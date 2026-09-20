/*
 * XhciHidMouseClaim.c — PR-S-xhcihid-1：复合设备鼠标认领
 * 从 XhciHid.c 原样搬家；不改语义。HID 全局仍在 XhciCore.c。
 */
#include "XHCI/XhciInternal.h"

/*
 * 在首次 ConfigEP 前：从当前配置描述符认领复合鼠标 iface（SetInterface/Protocol/Idle）。
 * 成功则写出 EP 参数供 ConfigureIntr 一次 Add。
 */
int PrepCompositeMouse(UINT16 Total, UINT8 Speed, UINT8 KbdIface, UINT8 KbdEp,
                              UINT8 *EpOut, UINT16 *MpsOut, UINT8 *IvOut) {
    UINT8 Iface = 0, EpAddr = 0, Interval = 10;
    UINT16 Mps = 8;
    UINT8 CurAlt = 0, BestAlt = 0;
    UINT16 Off;

    if (!ParseConfigMouse(gCtrlBuf, Total, Speed, &Iface, &EpAddr, &Mps, &Interval)) {
        return 0;
    }
    if (!HalCpuIsHypervisor() && gMouseParseScore < 2) {
        return 0;
    }
    if (Iface == KbdIface) {
        return 0;
    }
    if ((EpAddr & 0x0F) == (KbdEp & 0x0F) && ((EpAddr ^ KbdEp) & 0x80) == 0) {
        return 0;
    }

    Off = 0;
    while (Off + 9 <= Total) {
        UINT8 Len = gCtrlBuf[Off];
        UINT8 Type = gCtrlBuf[Off + 1];
        if (Len < 2 || Off + Len > Total) {
            break;
        }
        if (Type == 4 && Len >= 9) {
            CurAlt = gCtrlBuf[Off + 3];
            if (gCtrlBuf[Off + 2] == Iface && gCtrlBuf[Off + 5] == 3 &&
                gCtrlBuf[Off + 7] != 1) {
                BestAlt = CurAlt;
            }
        }
        Off = (UINT16)(Off + Len);
    }

    gMousePort = gPort1;
    gMouseIface = Iface;
    (void)SetInterface(Iface, BestAlt);
    if (gMouseIfaceProto == 1 || gMouseIfaceProto == 2) {
        (void)SetProtocolBoot(Iface);
    }
    (void)SetIdle(Iface);

    *EpOut = EpAddr;
    *MpsOut = Mps;
    *IvOut = Interval;
    {
        char Line[72];
        char Hex[12];
        int n = 0;
        const char *P = "Boot: XHCI mouse cfg i=";
        while (*P && n < 28) {
            Line[n++] = *P++;
        }
        HalSerialFormatHex(Hex, Iface, 2);
        P = Hex;
        while (*P && n < 40) {
            Line[n++] = *P++;
        }
        P = " p=";
        while (*P && n < 44) {
            Line[n++] = *P++;
        }
        HalSerialFormatHex(Hex, gMouseIfaceProto, 2);
        P = Hex;
        while (*P && n < 48) {
            Line[n++] = *P++;
        }
        P = " ep=";
        while (*P && n < 54) {
            Line[n++] = *P++;
        }
        HalSerialFormatHex(Hex, EpAddr, 2);
        P = Hex;
        while (*P && n < 58) {
            Line[n++] = *P++;
        }
        P = " mps=";
        while (*P && n < 64) {
            Line[n++] = *P++;
        }
        HalSerialFormatHex(Hex, Mps, 2);
        P = Hex;
        while (*P && n < 68) {
            Line[n++] = *P++;
        }
        Line[n++] = '\n';
        Line[n] = 0;
        BootLog(Line);
    }
    return 1;
}

/*
 * v8：G102 等被 skip-as-kbd 时，slot 已 Address 在 gDevCtx/gEp0。
 * 勿 Disable+再 Address（真机常 cc=0x04 / Reset 超时 → m=0）。
 * 直接把当前 slot 认领为独立鼠标。
 */
int ClaimAddressedSlotAsMouse(UINT32 RootPort, UINT8 Speed, UINT16 Total,
                                     UINT8 ConfigVal) {
    UINT8 Iface = 0, EpAddr = 0, Interval = 10;
    UINT16 Mps = 8;
    UINT32 Slot;

    if (HalCpuIsHypervisor() || gSlotId == 0 || gMouseSlotId != 0) {
        return 0;
    }
    if (!ParseConfigMouse(gCtrlBuf, Total, Speed, &Iface, &EpAddr, &Mps, &Interval)) {
        return 0;
    }
    if (gMouseParseScore < 2) {
        return 0;
    }

    Slot = gSlotId;
    CopyMemory(gMouseDevCtx, gDevCtx, sizeof(gMouseDevCtx));
    FlushDma(gMouseDevCtx, sizeof(gMouseDevCtx));
    DcbaaSet(Slot, PointerToPhysical(gMouseDevCtx));
    DcbaaFlush();

    gMouseSlotId = Slot;
    gMousePort = RootPort;
    gMouseRoute = gKbdRoute & 0xFFFFFu;
    gMouseHubSlot = gKbdHubSlot;
    gMouseTtPort = gKbdTtPort;
    gMouseIface = Iface;
    gMouseAbsolute = 0;
    /* gSlotEp0UsesKbdRing[Slot] 保持 1：EP0 仍用 Address 时的 gEp0 */

    gSlotId = 0;
    gIntrDci = 0;
    gKbdIface = 0;
    gKbdRoute = 0;
    gKbdHubSlot = 0;
    gKbdTtPort = 0;

    gXferSlot = gMouseSlotId;
    if (ConfigVal == 0) {
        ConfigVal = 1;
    }
    if (SetConfig(ConfigVal) < 0) {
        BootLog("Boot: XHCI mouse claim SetConfig fail\n");
        DisableSlot(gMouseSlotId);
        gMouseSlotId = 0;
        return 0;
    }
    if (gMouseIfaceProto == 1 || gMouseIfaceProto == 2) {
        (void)SetProtocolBoot(Iface);
    }
    if (gMouseIfaceProto == 1 || gMouseIfaceProto == 2 || gMouseIfaceProto == 0xFF) {
        SetIdle(Iface);
    }
    if (!ConfigureMouseIntr(gMouseSlotId, EpAddr, Mps, Interval, Speed)) {
        BootLog("Boot: XHCI mouse claim ConfigEP fail\n");
        DisableSlot(gMouseSlotId);
        gMouseSlotId = 0;
        return 0;
    }
    ZeroMemory(gMouseBuf, sizeof(gMouseBuf));
    QueueMouseIntr();
    BootLogHexV("Boot: XHCI mouse claim score=", gMouseParseScore, 2);
    BootLog("Boot: XHCI-HID Mouse (Claim After Skip-Kbd)\n");
    return 1;
}
