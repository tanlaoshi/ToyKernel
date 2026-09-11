/*
 * XhciMouse.c — PR-H-xhci-split-7：鼠标绑定 / 报告队列 / 对外 API
 *
 * 从单体 XHCI.c 原样搬家；不改语义。
 * ClaimAddressedSlotAsMouse 已在 split-5 落于 XhciHid.c。
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
        BootLogHexV("boot: xhci skip weak composite score=", gMouseParseScore, 2);
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

    BootLogHexV("boot: xhci mouse iface=", Iface, 2);
    BootLogHexV("boot: xhci mouse proto=", gMouseIfaceProto, 2);
    BootLogHexV("boot: xhci mouse ep=", EpAddr, 2);
    BootLogHexV("boot: xhci mouse mps=", Mps, 2);
    /* 单行汇总：串口好抄 */
    {
        char Line[72];
        char Hex[12];
        int n = 0;
        const char *P = "boot: xhci mouse cfg i=";
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
            BootLog("boot: xhci composite mouse ep fail\n");
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
                BootLog("boot: xhci sync kbd after mouse\n");
            } else {
                BootLog("boot: xhci sync kbd after mouse fail\n");
            }
            QueueIntr();
        }
        QueueMouseIntr();
        BootLog("boot: xhci composite kbd rearm\n");
    }
    {
        char Line[80];
        int n = 0;
        const char *P = "boot: xhci kbd iface=";
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
        BootLog("boot: xhci-hid mouse (composite abs)\n");
    } else {
        BootLog("boot: xhci-hid mouse (composite)\n");
    }
    return 1;
}

int InitMouseOnPort(UINT32 Port1) {
    UINT32 Ps = ReadMmio32(gOperationalBase + PortReg(Port1));
    UINT16 Total;
    UINT32 WasSlot;
    int Force;
    UINT8 Speed;
    UINT8 DevClass;

    if (gPortNoHid & (1u << Port1)) {
        BootLogHexV("boot: xhci mouse skip port=", Port1, 2);
        return 0;
    }
    BootLogHexV("boot: xhci mouse try port=", Port1, 2);
    if (!(Ps & PORTSC_CCS)) {
        return 0;
    }
    /*
     * 仅对「本轮已 Address 再 Disable」的口强制 PR（否则 cc=0x04）。
     * 全口 Force PR + 长超时会空转很久，短按电源失效。
     */
    Force = (gPortNeedForcePr & (1u << Port1)) ? 1 : 0;
    if (!ResetPortEx(Port1, Force)) {
        return 0;
    }
    if (Force && !HalCpuIsHypervisor()) {
        StallMs(20);
    }
    Speed = PortSpeed(ReadMmio32(gOperationalBase + PortReg(Port1)));
    gMousePort = Port1;
    gMouseRoute = 0;
    gMouseHubSlot = 0;
    gMouseTtPort = 0;
    gMouseAbsolute = 0;
    gMouseIfaceProto = 0;

    if (!AddressDeviceOnPort(Port1, Speed, &gMouseSlotId, gMouseDevCtx, 0, 0, 0, 0, 0)) {
        gMouseSlotId = 0;
        /* 未强制过且 Address 失败：再 Force PR 试一次（真鼠口常见） */
        if (!Force && (gCmdCode == 4 || gCmdCode == 0x11)) {
            if (!ResetPortEx(Port1, 1)) {
                return 0;
            }
            if (!HalCpuIsHypervisor()) {
                StallMs(20);
            }
            Speed = PortSpeed(ReadMmio32(gOperationalBase + PortReg(Port1)));
            if (!AddressDeviceOnPort(Port1, Speed, &gMouseSlotId, gMouseDevCtx, 0, 0, 0, 0, 0)) {
                gMouseSlotId = 0;
                return 0;
            }
        } else {
            return 0;
        }
    }

    /*
     * 键盘已绑定时，其它根口上的 hub 不会再走 TryHubOnRootPort。
     * 勿把 hub 当 HID（SetConfig/SetIdle → Stall cc=6）；认领 hub 后扫子口鼠标。
     */
    gXferSlot = gMouseSlotId;
    if (GetDeviceDesc() < 0) {
        gPortNeedForcePr |= (1u << Port1);
        DisableSlot(gMouseSlotId);
        gMouseSlotId = 0;
        return 0;
    }
    DevClass = gCtrlBuf[4];
    /* Mass Storage / Wireless：非鼠标，快跳过，避免 SetupHid 超时拖死启动 */
    if (DevClass == 0x08 || DevClass == 0xE0) {
        BootLogHex("boot: xhci mouse skip class=", DevClass, 2);
        gPortNoHid |= (1u << Port1);
        DisableSlot(gMouseSlotId);
        gMouseSlotId = 0;
        return 0;
    }
    if (IsHubDeviceDesc()) {
        WasSlot = gMouseSlotId;
        gMouseSlotId = 0;
        BootLogV("boot: xhci mouse-scan hub (class 9)\n");
        if (ClaimHubOnRootPort(Port1, Speed, WasSlot)) {
            return EnumHubChildrenForMouse();
        }
        return 0;
    }
    if (GetDesc(0x0200, 0, 9, gCtrlBuf) == 0) {
        Total = (UINT16)(gCtrlBuf[2] | (gCtrlBuf[3] << 8));
        if (Total < 9) {
            Total = 9;
        }
        if (Total > sizeof(gCtrlBuf)) {
            Total = (UINT16)sizeof(gCtrlBuf);
        }
        /*
         * 完整配置描述符：真机部分设备大包会超时；失败则 RecoverEp0 后仍走
         * SetupHidDevice（其内部会再取描述符）。勿在 EP0 失步时直接放弃。
         */
        if (GetDesc(0x0200, 0, Total, gCtrlBuf) < 0) {
            BootLog("boot: xhci mouse cfg desc retry\n");
            RecoverEp0(gMouseSlotId);
            gXferSlot = gMouseSlotId;
        } else if (ConfigHasHubIface(gCtrlBuf, Total)) {
            WasSlot = gMouseSlotId;
            gMouseSlotId = 0;
            BootLog("boot: xhci mouse-scan hub (iface 9)\n");
            if (ClaimHubOnRootPort(Port1, Speed, WasSlot)) {
                return EnumHubChildrenForMouse();
            }
            return 0;
        }
    }

    if (!SetupHidDevice(gMouseSlotId, gMouseDevCtx, Speed, ParseConfigMouse, 1)) {
        DebugWrite("XHCI: mouse config failed\n");
        /* 再 Force PR + 重 Address 一次（port4 真鼠曾卡在 cfg） */
        if (!HalCpuIsHypervisor()) {
            UINT32 Old = gMouseSlotId;
            BootLogV("boot: xhci mouse root retry\n");
            DisableSlot(Old);
            gMouseSlotId = 0;
            if (ResetPortEx(Port1, 1)) {
                if (!HalCpuIsHypervisor()) {
                    StallMs(20);
                }
                Speed = PortSpeed(ReadMmio32(gOperationalBase + PortReg(Port1)));
                if (AddressDeviceOnPort(Port1, Speed, &gMouseSlotId, gMouseDevCtx,
                                        0, 0, 0, 0, 0) &&
                    SetupHidDevice(gMouseSlotId, gMouseDevCtx, Speed, ParseConfigMouse, 1)) {
                    /* fall through to EP setup below */
                } else {
                    if (gMouseSlotId) {
                        DisableSlot(gMouseSlotId);
                    }
                    gMouseSlotId = 0;
                    return 0;
                }
            } else {
                return 0;
            }
        } else {
            gPortNeedForcePr |= (1u << Port1);
            DisableSlot(gMouseSlotId);
            gMouseSlotId = 0;
            return 0;
        }
    }

    UINT8 EpAddr = 0, Interval = 10;
    UINT16 Mps = 8;
    UINT8 Iface = 0;
    if (GetDesc(0x0200, 0, 9, gCtrlBuf) < 0) {
        DisableSlot(gMouseSlotId);
        gMouseSlotId = 0;
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
        RecoverEp0(gMouseSlotId);
        gXferSlot = gMouseSlotId;
        if (GetDesc(0x0200, 0, Total, gCtrlBuf) < 0) {
            DisableSlot(gMouseSlotId);
            gMouseSlotId = 0;
            return 0;
        }
    }
    if (!ParseConfigMouse(gCtrlBuf, Total, Speed, &Iface, &EpAddr, &Mps, &Interval)) {
        DebugWrite("XHCI: mouse no interrupt EP\n");
        BootLog("boot: xhci skip non-mouse HID\n");
        DisableSlot(gMouseSlotId);
        gMouseSlotId = 0;
        return 0;
    }
    if (!HalCpuIsHypervisor() && gMouseParseScore < 2) {
                BootLogHexV("boot: xhci skip weak root mouse score=", gMouseParseScore, 2);
        DisableSlot(gMouseSlotId);
        gMouseSlotId = 0;
        return 0;
    }
    gMouseIface = Iface;
    BootLogHexV("boot: xhci mouse root score=", gMouseParseScore, 2);
    if (!ConfigureMouseIntr(gMouseSlotId, EpAddr, Mps, Interval, Speed)) {
        DebugWrite("XHCI: mouse endpoint failed\n");
        DisableSlot(gMouseSlotId);
        gMouseSlotId = 0;
        return 0;
    }
    ZeroMemory(gMouseBuf, sizeof(gMouseBuf));
    QueueMouseIntr();
    DebugWrite("XHCI: mouse ready\n");
    if (gMouseAbsolute) {
        BootLog("boot: xhci-hid mouse (abs)\n");
    } else {
        BootLog("boot: xhci-hid mouse\n");
    }
    return 1;
}

/* 真机 PHOTO 后再绑鼠标，避免复合/hub 扫描踩键盘 EP */
void XhciInitMouseDeferred(void) {
    if (HalCpuIsHypervisor()) {
        return;
    }
    if (gMouseSlotId != 0) {
        return;
    }
    ToyLogUsb("boot: xhci mouse deferred start\n");
    if (gHubSlotId != 0) {
        (void)EnumHubChildrenForMouse();
    }
    if (gMouseSlotId == 0) {
        for (UINT32 p = 1; p <= gMaxPorts; p++) {
            if (gSlotId != 0 && p == gPort1) {
                continue;
            }
            if (InitMouseOnPort(p)) {
                break;
            }
        }
    }
    if (gMouseSlotId == 0) {
        (void)InitMouseOnKeyboardSlot(); /* 回退：保鼠标 */
    }
    if (gMouseSlotId == 0 && gHubSlotId != 0) {
        (void)EnumHubChildrenForMouse();
    }
    if (gMouseSlotId != 0 && gMouseIntrDci != 0) {
        QueueMouseIntr();
    }
    if (gSlotId != 0 && gIntrDci != 0) {
        QueueIntr();
    }
    if (gMouseSlotId != 0) {
        ToyLogUsb("boot: xhci mouse deferred ok\n");
    } else {
        ToyLogUsb("boot: xhci mouse deferred none\n");
    }
}

void MousePush(void) {
    UINT32 Next = (gMouseWriteIndex + 1) % MOUSE_Q;
    UINT32 X0;
    UINT32 Y0;
    UINT32 X1;
    UINT32 Y1;
    int UseAbsolute;
    UINT8 ParseLen;

    if (Next == gMouseReadIndex) {
        return;
    }
    USB_MOUSE_REPORT *R = &gMouseQ[gMouseWriteIndex];
    R->Wheel = 0;
    R->Absolute = 0;
    ParseLen = gMouseXferLen ? gMouseXferLen : gMouseReportLen;
    if (ParseLen < 3) {
        ParseLen = 3;
    }
    if (ParseLen > 8) {
        ParseLen = 8;
    }
    X0 = (UINT32)(gMouseBuf[1] | (gMouseBuf[2] << 8));
    Y0 = (UINT32)(gMouseBuf[3] | (gMouseBuf[4] << 8));
    X1 = (UINT32)(gMouseBuf[2] | (gMouseBuf[3] << 8));
    Y1 = (UINT32)(gMouseBuf[4] | (gMouseBuf[5] << 8));

    /*
     * 仅枚举标了 gMouseAbsolute（QEMU tablet）才走绝对。
     * boot 相对鼠（Proto=2）禁止「高字节启发式」——短包残留曾把 dx/dy
     * 当成 16-bit 绝对坐标，Gui 再 /32767 → 光标钉死在角上（PHOTO m 涨、桌面不动）。
     */
    UseAbsolute = 0;
    if (gMouseAbsolute && gMouseIfaceProto != 2) {
        if (ParseLen >= 5 && X0 <= 32767 && Y0 <= 32767) {
            UseAbsolute = 1;
        } else if (ParseLen >= 6 && gMouseBuf[0] != 0 && X1 <= 32767 &&
                   Y1 <= 32767) {
            UseAbsolute = 2;
        }
    }

    if (UseAbsolute == 1) {
        R->Buttons = gMouseBuf[0] & 7;
        R->X = X0;
        R->Y = Y0;
        R->Absolute = 1;
        if (ParseLen >= 6) {
            R->Wheel = (INT8)gMouseBuf[5];
        }
    } else if (UseAbsolute == 2) {
        R->Buttons = gMouseBuf[1] & 7;
        R->X = X1;
        R->Y = Y1;
        R->Absolute = 1;
        if (ParseLen >= 7) {
            R->Wheel = (INT8)gMouseBuf[6];
        }
    } else {
        /* HID boot 相对鼠标：b0 buttons, b1 X, b2 Y, b3 wheel */
        int Dx = (int)(signed char)gMouseBuf[1];
        int Dy = (int)(signed char)gMouseBuf[2];
        if (!gMouseAbsInit) {
            gMouseAbsInit = 1;
        }
        gMouseAbsX += Dx;
        gMouseAbsY += Dy;
        if (gMouseAbsX < 0) {
            gMouseAbsX = 0;
        }
        if (gMouseAbsY < 0) {
            gMouseAbsY = 0;
        }
        if (gMouseAbsX > 3840) {
            gMouseAbsX = 3840;
        }
        if (gMouseAbsY > 2160) {
            gMouseAbsY = 2160;
        }
        R->X = (UINT32)gMouseAbsX;
        R->Y = (UINT32)gMouseAbsY;
        R->Buttons = gMouseBuf[0] & 7;
        if (ParseLen >= 4) {
            R->Wheel = (INT8)gMouseBuf[3];
        }
    }
    gMouseWriteIndex = Next;
}

int XhciMousePresent(void) {
    return gMouseSlotId != 0;
}

/*
 * PHOTO 里 HalInputPoll 只 Push 不消费 → 鼠队列易满。
 * 进桌面前只抽空队列并对齐 Abs；勿 SyncIntrDequeue（枚举后多余 Stop 曾致 PHOTO r=0）。
 */
void XhciMouseHandoffDesktop(UINT32 CursorX, UINT32 CursorY) {
    if (HalCpuIsHypervisor()) {
        return;
    }
    SpinLockAcquire(&gHidQueueLock);
    gMouseReadIndex = gMouseWriteIndex;
    gMouseAbsX = (int)CursorX;
    gMouseAbsY = (int)CursorY;
    gMouseAbsInit = 1;
    if (gMouseAbsX < 0) {
        gMouseAbsX = 0;
    }
    if (gMouseAbsY < 0) {
        gMouseAbsY = 0;
    }
    SpinLockRelease(&gHidQueueLock);
    ToyLogUsb("boot: xhci mouse handoff desktop\n");
}

int XhciDequeueMouse(USB_MOUSE_REPORT *Report) {
    int Ok = 0;

    SpinLockAcquire(&gHidQueueLock);
    if (gMouseReadIndex != gMouseWriteIndex) {
        *Report = gMouseQ[gMouseReadIndex];
        gMouseReadIndex = (gMouseReadIndex + 1) % MOUSE_Q;
        Ok = 1;
    }
    SpinLockRelease(&gHidQueueLock);
    return Ok;
}

