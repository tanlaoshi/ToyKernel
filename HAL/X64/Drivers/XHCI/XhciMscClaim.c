/*
 * XhciMscClaim.c — MSC FinishClaim / ConfigEP（PR-S3-xhcimscclaim-1）
 *
 * 配置描述符解析见 XhciMscClaimParse.c。MSC 全局仍在 Xhci.c。
 */
#include "XHCI/XhciInternal.h"

/* ConfigEP：Bulk IN + Bulk OUT（EP Type 6/2）；带回 Route/TT */
static int ConfigureMscBulk(UINT32 SlotId, UINT32 RootPort, UINT8 Speed,
                            UINT8 EpIn, UINT16 MpsIn, UINT8 EpOut, UINT16 MpsOut) {
    UINT8 InNum = EpIn & 0x0F;
    UINT8 OutNum = EpOut & 0x0F;
    UINT32 InDci = (UINT32)InNum * 2 + 1;
    UINT32 OutDci = (UINT32)OutNum * 2 + 0;
    UINT32 CtxEntries = InDci > OutDci ? InDci : OutDci;
    UINT32 *Slot;
    UINT32 *Ep;
    UINT64 Deq;

    if (MpsIn == 0 || MpsIn > 1024) {
        MpsIn = 512;
    }
    if (MpsOut == 0 || MpsOut > 1024) {
        MpsOut = 512;
    }

    ZeroMemory(gInCtx, sizeof(gInCtx));
    *(UINT32 *)(void *)(gInCtx + 4) = (1u << 0) | (1u << InDci) | (1u << OutDci);

    Slot = (UINT32 *)(void *)InSlot();
    Slot[0] = (CtxEntries << 27) | ((UINT32)Speed << 20) | (gMscRoute & 0xFFFFFu);
    Slot[1] = (UINT32)RootPort << 16;
    if (gMscHubSlot != 0 && Speed < 3) {
        Slot[2] = (UINT32)gMscHubSlot | ((UINT32)gMscTtPort << 8);
    }

    if (!gMscBulkRingsInited) {
        (void)XhciMscBringUp();
    }
    InitRing(gBulkInRing, &gBulkIn, RING_SIZE);
    InitRing(gBulkOutRing, &gBulkOut, RING_SIZE);

    Ep = (UINT32 *)(void *)InEp(InDci);
    Ep[0] = 0;
    Ep[1] = (3u << 1) | (6u << 3) | ((UINT32)MpsIn << 16); /* Bulk IN */
    Deq = PointerToPhysical(gBulkInRing) | 1;
    Ep[2] = (UINT32)Deq;
    Ep[3] = (UINT32)(Deq >> 32);
    Ep[4] = (UINT32)MpsIn;

    Ep = (UINT32 *)(void *)InEp(OutDci);
    Ep[0] = 0;
    Ep[1] = (3u << 1) | (2u << 3) | ((UINT32)MpsOut << 16); /* Bulk OUT */
    Deq = PointerToPhysical(gBulkOutRing) | 1;
    Ep[2] = (UINT32)Deq;
    Ep[3] = (UINT32)(Deq >> 32);
    Ep[4] = (UINT32)MpsOut;

    FlushDma(gInCtx, sizeof(gInCtx));
    FlushDma(gBulkInRing, sizeof(gBulkInRing));
    FlushDma(gBulkOutRing, sizeof(gBulkOutRing));
    FlushDma(gMscScanDevCtx, sizeof(gMscScanDevCtx));

    if (Command(PointerToPhysical(gInCtx), TRB_TYPE(TRB_CONFIG_EP) | TRB_SLOT(SlotId), 0) < 0) {
        BootLog("Boot: MSC claim cfg ep fail\n");
        return 0;
    }

    gMscBulkInDci = InDci;
    gMscBulkOutDci = OutDci;
    gMscBulkInMps = MpsIn;
    gMscBulkOutMps = MpsOut;
    return 1;
}

/*
 * gMscScanSlot 已 Address：取配置、Parse Bulk、SetConfig、ConfigEP。
 * 成功置 gMscClaimed；失败 Disable slot。
 */
int XhciMscFinishClaim(UINT32 RootPort, UINT8 Speed) {
    UINT8 EpIn = 0;
    UINT8 EpOut = 0;
    UINT8 Iface = 0;
    UINT16 MpsIn = 64;
    UINT16 MpsOut = 64;
    UINT16 Total;
    UINT8 ConfigVal = 1;
    UINT8 DevClass;

    if (gMscScanSlot == 0) {
        return 0;
    }
    gXferSlot = gMscScanSlot;
    if (GetDeviceDesc() < 0) {
        BootLog("Boot: MSC claim desc fail\n");
        goto fail;
    }
    DevClass = gCtrlBuf[4];
    BootLogHex("Boot: MSC claim dclass=", DevClass, 2);
    {
        UINT16 Vid = (UINT16)(gCtrlBuf[8] | (gCtrlBuf[9] << 8));

        /* 勿把 FT232 当 MSC（厂商 Bulk）；留给 usb-uart */
        if (Vid == 0x0403u) {
            BootLog("Boot: MSC claim skip FTDI\n");
            goto fail;
        }
    }
    if (DevClass == 0x09) {
        BootLog("Boot: MSC claim skip hub device\n");
        goto fail;
    }
    if (DevClass == 0x02) {
        BootLog("Boot: MSC claim skip CDC\n");
        goto fail;
    }
    if (DevClass == 0x03 || DevClass == 0xE0) {
        BootLogHex("Boot: MSC claim skip class=", DevClass, 2);
        goto fail;
    }

    if (GetDesc(USB_WVALUE_DT_CONFIG, 0, 9, gMscCfgBuf) < 0) {
        BootLog("Boot: MSC claim cfg9 fail\n");
        goto fail;
    }
    Total = (UINT16)(gMscCfgBuf[2] | (gMscCfgBuf[3] << 8));
    if (Total < 9) {
        Total = 9;
    }
    if (Total > sizeof(gMscCfgBuf)) {
        BootLogHex("Boot: MSC claim cfg trunc want=", Total, 4);
        Total = (UINT16)sizeof(gMscCfgBuf);
    }
    ConfigVal = gMscCfgBuf[5] ? gMscCfgBuf[5] : 1;
    if (GetDesc(USB_WVALUE_DT_CONFIG, 0, Total, gMscCfgBuf) < 0) {
        RecoverEp0(gMscScanSlot);
        gXferSlot = gMscScanSlot;
        if (GetDesc(USB_WVALUE_DT_CONFIG, 0, Total, gMscCfgBuf) < 0) {
            BootLog("Boot: MSC claim cfg fail\n");
            goto fail;
        }
    }
    BootLogHex("Boot: MSC claim cfg len=", Total, 4);

    if (ConfigHasHubIface(gMscCfgBuf, Total)) {
        /* 根口应在 ClaimPorts 已认领；子口嵌套 hub 跳过 */
        BootLogHex("Boot: MSC claim cfg hub iface port=", RootPort, 2);
        goto fail;
    }

    if (!ParseMscBulk(gMscCfgBuf, Total, &Iface, &EpIn, &MpsIn, &EpOut, &MpsOut)) {
        BootLogHex("Boot: MSC claim no bulk port=", RootPort, 2);
        LogMscCfgIfaces(gMscCfgBuf, Total);
        goto fail;
    }

    if (SetConfig(ConfigVal) < 0) {
        BootLog("Boot: MSC claim setcfg fail\n");
        goto fail;
    }

    if (!ConfigureMscBulk(gMscScanSlot, RootPort, Speed, EpIn, MpsIn, EpOut, MpsOut)) {
        goto fail;
    }

    gMscPort = RootPort;
    gMscClaimed = 1;
    BootLogHex("Boot: MSC claim ok port=", RootPort, 2);
    BootLogHex("Boot: MSC claim slot=", gMscScanSlot, 2);
    BootLogHex("Boot: MSC claim iface=", Iface, 2);
    BootLogHex("Boot: MSC claim epin=", EpIn, 2);
    BootLogHex("Boot: MSC claim epout=", EpOut, 2);
    BootLogHex("Boot: MSC claim route=", gMscRoute, 2);
    BootLog("Boot: MSC claim bulk ok\n");
    return 1;

fail:
    if (gMscScanSlot != 0) {
        gPortNeedForcePr |= (1u << RootPort);
        DisableSlot(gMscScanSlot);
        gMscScanSlot = 0;
    }
    return 0;
}
