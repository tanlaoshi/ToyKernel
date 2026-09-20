/*
 * XhciMscClaim.c — PR-S-xhcimsc-1：从 XhciMsc.c 原样搬家；不改语义。
 * MSC 全局仍定义在 XhciCore.c（BSS 顺序影响 HID DMA 环；勿迁出）。
 */
#include "XHCI/XhciInternal.h"

/* 配置描述符中找 MSC Bulk IN/OUT（偏好 BOT；允许 UASP；兜底任一对 Bulk） */
static int ParseMscBulk(UINT8 *Cfg, UINT16 Total, UINT8 *OutIface,
                       UINT8 *EpIn, UINT16 *MpsIn, UINT8 *EpOut, UINT16 *MpsOut) {
    UINT16 Off = 0;
    UINT8 CurIface = 0xFF;
    UINT8 CurAlt = 0;
    UINT8 IfaceClass = 0;
    UINT8 IfaceSub = 0;
    UINT8 IfaceProto = 0;
    UINT8 BestIface = 0xFF;
    UINT8 BestIn = 0;
    UINT8 BestOut = 0;
    UINT16 BestInMps = 0;
    UINT16 BestOutMps = 0;
    int BestScore = -1;

    while (Off + 2 <= Total) {
        UINT8 Len = Cfg[Off];
        UINT8 Type = Cfg[Off + 1];

        if (Len < 2 || Off + Len > Total) {
            break;
        }
        if (Type == 4 && Len >= 9) {
            CurIface = Cfg[Off + 2];
            CurAlt = Cfg[Off + 3];
            IfaceClass = Cfg[Off + 5];
            IfaceSub = Cfg[Off + 6];
            IfaceProto = Cfg[Off + 7];
        } else if (Type == 5 && Len >= 7 && CurIface != 0xFF && CurAlt == 0) {
            UINT8 Addr = Cfg[Off + 2];
            UINT8 Attr = Cfg[Off + 3];
            UINT16 Mps = (UINT16)(Cfg[Off + 4] | (Cfg[Off + 5] << 8));
            int Score;

            if ((Attr & 0x03) != 2) {
                Off = (UINT16)(Off + Len);
                continue;
            }
            /* class8 BOT/UASP 优先；其它 iface 仅作兜底（score 0） */
            if (IfaceClass == 0x08) {
                Score = 10;
                if (IfaceSub == 0x06) {
                    Score += 2;
                }
                if (IfaceProto == 0x50 || IfaceProto == 0x62) {
                    Score += 4;
                }
            } else {
                Score = 0;
            }
            if (Score < BestScore) {
                Off = (UINT16)(Off + Len);
                continue;
            }
            if (Score > BestScore) {
                BestScore = Score;
                BestIface = CurIface;
                BestIn = 0;
                BestOut = 0;
                BestInMps = 0;
                BestOutMps = 0;
            } else if (CurIface != BestIface) {
                Off = (UINT16)(Off + Len);
                continue;
            }
            if (Addr & 0x80) {
                BestIn = Addr;
                BestInMps = Mps ? Mps : 64;
            } else {
                BestOut = Addr;
                BestOutMps = Mps ? Mps : 64;
            }
        }
        Off = (UINT16)(Off + Len);
    }

    /* 兜底 score=0 须成对 Bulk；class8 同 */
    if (BestIn == 0 || BestOut == 0) {
        return 0;
    }
    if (BestScore < 0) {
        return 0;
    }
    /* 无 class8 时仅当找到成对 Bulk 才接受（score 0） */
    if (OutIface) {
        *OutIface = BestIface;
    }
    if (EpIn) {
        *EpIn = BestIn;
    }
    if (MpsIn) {
        *MpsIn = BestInMps;
    }
    if (EpOut) {
        *EpOut = BestOut;
    }
    if (MpsOut) {
        *MpsOut = BestOutMps;
    }
    return 1;
}

static void LogMscCfgIfaces(UINT8 *Cfg, UINT16 Total) {
    UINT16 Off = 0;
    int N = 0;

    while (Off + 9 <= Total && N < 6) {
        UINT8 Len = Cfg[Off];
        UINT8 Type = Cfg[Off + 1];

        if (Len < 2 || Off + Len > Total) {
            break;
        }
        if (Type == 4 && Len >= 9) {
            /* 一行汇总，避免 iface/iclass/isub/iproto 四连刷屏夹空行感 */
            BootLogHex("Boot: MSC claim iface=", Cfg[Off + 2], 2);
            N++;
        }
        Off = (UINT16)(Off + Len);
    }
}

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
    if (DevClass == 0x09) {
        BootLog("Boot: MSC claim skip hub device\n");
        goto fail;
    }
    if (DevClass == 0x03 || DevClass == 0xE0) {
        BootLogHex("Boot: MSC claim skip class=", DevClass, 2);
        goto fail;
    }

    if (GetDesc(0x0200, 0, 9, gMscCfgBuf) < 0) {
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
    if (GetDesc(0x0200, 0, Total, gMscCfgBuf) < 0) {
        RecoverEp0(gMscScanSlot);
        gXferSlot = gMscScanSlot;
        if (GetDesc(0x0200, 0, Total, gMscCfgBuf) < 0) {
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
