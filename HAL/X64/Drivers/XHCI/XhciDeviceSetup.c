/*
 * XhciDeviceSetup.c — PR-S-xhcidevice-1：Set* / SetupHidDevice
 *
 * 从 XhciDevice.c 原样搬家；不改语义。无 static 提升。
 */
#include "XHCI/XhciInternal.h"

int SetConfig(UINT8 Config) {
    USB_SETUP_PACKET Setup = {
        .bmRequestType = 0x00,
        .bRequest = 0x09,
        .wValue = Config,
        .wIndex = 0,
        .wLength = 0
    };
    return ControlXfer(&Setup, 0);
}

/* HID SET_PROTOCOL Boot 协议 */
int SetProtocolBoot(UINT8 Iface) {
    USB_SETUP_PACKET Setup = {
        .bmRequestType = 0x21,
        .bRequest = 0x0B,
        .wValue = 0x0000,
        .wIndex = Iface,
        .wLength = 0
    };
    return ControlXfer(&Setup, 0);
}

/* HID SET_IDLE 请求 */
int SetIdle(UINT8 Iface) {
    USB_SETUP_PACKET Setup = {
        .bmRequestType = 0x21,
        .bRequest = 0x0A,
        .wValue = 0x0000,
        .wIndex = Iface,
        .wLength = 0
    };
    return ControlXfer(&Setup, 0);
}

/* HID SET_REPORT：输出报告（键盘 LED 等） */
int SetReportOutput(UINT8 Iface, void *Data, UINT16 Length) {
    USB_SETUP_PACKET Setup = {
        .bmRequestType = 0x21,
        .bRequest = 0x09,
        .wValue = 0x0200,
        .wIndex = Iface,
        .wLength = Length
    };
    return ControlXfer(&Setup, Data);
}

/* HID SET_INTERFACE：激活指定 Alternate（复合键鼠偶见鼠标在 alt>0） */
int SetInterface(UINT8 Iface, UINT8 Alt) {
    USB_SETUP_PACKET Setup = {
        .bmRequestType = 0x01,
        .bRequest = 0x0B,
        .wValue = Alt,
        .wIndex = Iface,
        .wLength = 0
    };
    return ControlXfer(&Setup, 0);
}

/* HID：取配置描述符、SetConfig、可选 Boot Protocol / SET_IDLE */
int SetupHidDevice(UINT32 SlotId, UINT8 *DevCtx, UINT8 Speed,
                   int (*ParseFn)(UINT8 *, UINT16, UINT8, UINT8 *, UINT8 *,
                                  UINT16 *, UINT8 *),
                   int UseBootProto) {
    gXferSlot = SlotId;
    (void)DevCtx;

    if (!HalCpuIsHypervisor()) {
        StallMs(10);
    } else {
        for (volatile int d = 0; d < 500000; d++) {
        }
    }

    /* Address 已建好本 slot 的 EP0 环；勿 InitRing/SetTrDeq 打断 Running EP0 */

    if (GetDeviceDesc() < 0) {
        return 0;
    }
    if (GetDesc(0x0200, 0, 9, gCtrlBuf) < 0) {
        return 0;
    }
    UINT16 Total = (UINT16)(gCtrlBuf[2] | (gCtrlBuf[3] << 8));
    if (Total < 9) {
        Total = 9;
    }
    if (Total > sizeof(gCtrlBuf)) {
        Total = (UINT16)sizeof(gCtrlBuf);
    }
    if (GetDesc(0x0200, 0, Total, gCtrlBuf) < 0) {
        return 0;
    }
    UINT8 ConfigVal = gCtrlBuf[5];
    if (ConfigVal == 0) {
        ConfigVal = 1;
    }

    UINT8 Iface = 0, EpAddr = 0, Interval = 10;
    UINT16 Mps = 8;
    UINT8 IfaceProto = 0xFF;
    int HaveIntr = ParseFn(gCtrlBuf, Total, Speed, &Iface, &EpAddr, &Mps, &Interval);
    if (SetConfig(ConfigVal) < 0) {
        return 0;
    }
    /*
     * SET_PROTOCOL(Boot) 仅对 Boot 接口（kbd Proto=1 / mouse Proto=2）。
     * QEMU usb-tablet 为 Proto=0：发 SET_PROTOCOL 会 Stall(cc=6)，EP0 随后
     * GetDesc 全失败 → 鼠标 DisableSlot，日志只有 keyboard 没有 mouse。
     */
    if (UseBootProto) {
        UINT16 Off = 0;
        while (Off + 9 <= Total) {
            UINT8 Len = gCtrlBuf[Off];
            UINT8 Type = gCtrlBuf[Off + 1];
            if (Len < 2 || Off + Len > Total) {
                break;
            }
            if (Type == 4 && Len >= 9 && gCtrlBuf[Off + 2] == Iface) {
                IfaceProto = gCtrlBuf[Off + 7];
                break;
            }
            Off = (UINT16)(Off + Len);
        }
        if (IfaceProto == 1 || IfaceProto == 2) {
            SetProtocolBoot(Iface);
        }
    }
    /* SET_IDLE(0)：部分 boot 鼠无此则中断 IN 不吐报告；与共享 EP0 环问题正交 */
    if (IfaceProto == 1 || IfaceProto == 2 || IfaceProto == 0xFF) {
        SetIdle(Iface);
    }
    return HaveIntr;
}

