/*
 * XhciMousePort.c — PR-S-xhcimouse-1：其它根口绑鼠标
 *
 * 从 XhciMouse.c 原样搬家；不改语义。无 static 提升；鼠标全局仍在 Xhci.c。
 */
#include "XHCI/XhciInternal.h"

int InitMouseOnPort(UINT32 Port1) {
    UINT32 Ps = ReadMmio32(gOperationalBase + PortReg(Port1));
    UINT16 Total;
    UINT32 WasSlot;
    int Force;
    UINT8 Speed;
    UINT8 DevClass;

    if (gPortNoHid & (1u << Port1)) {
        BootLogHexV("Boot: XHCI mouse skip port=", Port1, 2);
        return 0;
    }
    BootLogHexV("Boot: XHCI mouse try port=", Port1, 2);
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
        BootLogHex("Boot: XHCI mouse skip class=", DevClass, 2);
        gPortNoHid |= (1u << Port1);
        DisableSlot(gMouseSlotId);
        gMouseSlotId = 0;
        return 0;
    }
    if (IsHubDeviceDesc()) {
        WasSlot = gMouseSlotId;
        gMouseSlotId = 0;
        BootLogV("Boot: XHCI mouse-scan hub (class 9)\n");
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
            BootLog("Boot: XHCI mouse cfg desc retry\n");
            RecoverEp0(gMouseSlotId);
            gXferSlot = gMouseSlotId;
        } else if (ConfigHasHubIface(gCtrlBuf, Total)) {
            WasSlot = gMouseSlotId;
            gMouseSlotId = 0;
            BootLog("Boot: XHCI mouse-scan hub (iface 9)\n");
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
            BootLogV("Boot: XHCI mouse root retry\n");
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
        BootLog("Boot: XHCI skip non-mouse HID\n");
        DisableSlot(gMouseSlotId);
        gMouseSlotId = 0;
        return 0;
    }
    if (!HalCpuIsHypervisor() && gMouseParseScore < 2) {
                BootLogHexV("Boot: XHCI skip weak root mouse score=", gMouseParseScore, 2);
        DisableSlot(gMouseSlotId);
        gMouseSlotId = 0;
        return 0;
    }
    gMouseIface = Iface;
    BootLogHexV("Boot: XHCI mouse root score=", gMouseParseScore, 2);
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
        BootLog("Boot: XHCI-HID Mouse (Abs)\n");
    } else {
        BootLog("Boot: XHCI-HID Mouse\n");
    }
    return 1;
}

