/*
 * XhciMouseComposite.c — PR-S-xhcimouse-1：复合设备同 slot 绑鼠标
 *
 * 从 XhciMouse.c 原样搬家；不改语义。无 static 提升；鼠标全局仍在 Xhci.c。
 */
#include "XHCI/XhciInternal.h"

/*
 * 真机常见：USB 键鼠复合设备（同一 slot 上键盘 Proto=1 + 鼠标 Proto=2）。
 * 旧逻辑只扫「其它根口」，同口第二接口永远绑不上 → arms mouse=00/00。
 */
int InitMouseOnKeyboardSlot(void) {
    UINT8 EpAddr = 0, Interval = 10, Iface = 0;
    UINT16 Mps = 8;
    UINT16 Total;
    UINT8 CurAlt = 0;
    UINT8 BestAlt = 0;
    UINT16 Off;

    if (gSlotId == 0 || gMouseSlotId != 0) {
        return 0;
    }

    gXferSlot = gSlotId;
    if (GetDesc(0x0200, 0, 9, gCtrlBuf) < 0) {
        return 0;
    }
    Total = (UINT16)(gCtrlBuf[2] | (gCtrlBuf[3] << 8));
    if (Total < 9) {
        Total = 9;
    }
    if (Total > sizeof(gCtrlBuf)) {
        Total = (UINT16)sizeof(gCtrlBuf);
    }
    if (GetDesc(0x0200, 0, Total, gCtrlBuf) < 0) {
        return 0;
    }
    if (!ParseConfigMouse(gCtrlBuf, Total, gSpeed, &Iface, &EpAddr, &Mps, &Interval)) {
        return 0;
    }
    /*
     * 真机：拒绝弱评分（多为键盘上的 media/vendor HID，proto=0 且永不报指针）。
     * QEMU tablet 走独立口 InitMouseOnPort，不受此限。
     */
    if (!HalCpuIsHypervisor() && gMouseParseScore < 2) {
        BootLogHexV("Boot: XHCI skip weak composite score=", gMouseParseScore, 2);
        return 0;
    }
    if (Iface == gKbdIface) {
        return 0;
    }
    if ((EpAddr & 0x0F) == (gKbdEpAddr & 0x0F) && ((EpAddr ^ gKbdEpAddr) & 0x80) == 0) {
        return 0;
    }

    /* 找回该 iface 的 bAlternateSetting（Parse 未导出） */
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

    BootLogHexV("Boot: XHCI mouse iface=", Iface, 2);
    BootLogHexV("Boot: XHCI mouse proto=", gMouseIfaceProto, 2);
    BootLogHexV("Boot: XHCI mouse ep=", EpAddr, 2);
    BootLogHexV("Boot: XHCI mouse mps=", Mps, 2);
    /* 单行汇总：串口好抄 */
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
        BootLog(Line); /* 真机 BootMark → COM1 + 屏 */
    }

    {
        int MouseCfg;

        MouseCfg = ConfigureMouseIntr(gSlotId, EpAddr, Mps, Interval, gSpeed);
        if (!MouseCfg) {
            BootLog("Boot: XHCI composite mouse ep fail\n");
            gMouseIntrDci = 0;
            gMouseEpAddr = 0;
            return 0;
        }

        gMouseSlotId = gSlotId;
        ZeroMemory(gReportBuf, 8);
        ZeroMemory(gMouseBuf, sizeof(gMouseBuf));
        /*
         * MouseCfg：1=add-only 键盘未停 → 勿 Sync；2/3=曾 Stop/Drop → Sync 键盘。
         * Arm 同样勿再 Sync（PHOTO 上 Sync 后 k 仍 0）。
         */
        if (MouseCfg >= 2 && gSlotId != 0 && gIntrDci != 0) {
            if (SyncIntrDequeue(gSlotId, gIntrDci, gIntrRing, &gIntr, sizeof(gIntrRing)) == 0) {
                BootLog("Boot: XHCI sync kbd after mouse\n");
            } else {
                BootLog("Boot: XHCI sync kbd after mouse fail\n");
            }
            QueueIntr();
        }
        QueueMouseIntr();
        BootLog("Boot: XHCI composite kbd rearm\n");
    }
    {
        char Line[80];
        int n = 0;
        const char *P = "Boot: XHCI kbd iface=";
        while (*P && n < 24) {
            Line[n++] = *P++;
        }
        Line[n++] = (char)('0' + ((gKbdIface / 10) % 10));
        Line[n++] = (char)('0' + (gKbdIface % 10));
        P = " dci=";
        while (*P && n < 36) {
            Line[n++] = *P++;
        }
        Line[n++] = (char)('0' + ((gIntrDci / 10) % 10));
        Line[n++] = (char)('0' + (gIntrDci % 10));
        P = " mouse dci=";
        while (*P && n < 56) {
            Line[n++] = *P++;
        }
        Line[n++] = (char)('0' + ((gMouseIntrDci / 10) % 10));
        Line[n++] = (char)('0' + (gMouseIntrDci % 10));
        Line[n++] = '\n';
        Line[n] = 0;
        BootLog(Line);
    }
    if (gMouseAbsolute) {
        BootLog("Boot: XHCI-HID Mouse (Composite Abs)\n");
    } else {
        BootLog("Boot: XHCI-HID Mouse (Composite)\n");
    }
    return 1;
}

