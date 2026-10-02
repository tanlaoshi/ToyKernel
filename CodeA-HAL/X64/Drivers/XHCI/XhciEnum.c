/*
 * XhciEnum.c — PR-F-xhci-1：根口枚举键盘并绑鼠标（编排）
 *
 * 单口试探 / 绑鼠见 XhciEnumPort.c。BSS 仍在 Xhci.c。
 */
#include "XHCI/XhciInternal.h"

int XhciEnumAndBind(void) {
    int RealPc = !HalCpuIsHypervisor();
    UINT32 Port1 = 0;
    int PassMax = 3;

    gPortNoHid = 0;
    gPortNeedForcePr = 0;
    for (int Wait = 0; Wait < PassMax && Port1 == 0; Wait++) {
        if (DiagVerbose()) {
            ToyLogUsb("Boot: XHCI enum pass=");
            {
                char B[12];
                HalSerialFormatHex(B, (UINT64)(UINT32)(Wait + 1), 2);
                ToyLogUsb(B);
                ToyLogUsb("\n");
            }
        }
        for (UINT32 P = 1; P <= gMaxPorts && P <= 32; P++) {
            if (XhciEnumTryRootPort(P)) {
                Port1 = gPort1;
                break;
            }
        }
        if (Port1 == 0) {
            if (RealPc) {
                StallMs(50);
            } else {
                for (volatile int D = 0; D < 40000; D++) {
                }
            }
        }
    }

    if (Port1 == 0) {
        if (RealPc) {
            HalSerialGopMute(0); /* 放弃 xHCI：允许后续 boot 黄字 */
        }
        ToyLogUsb("Boot: XHCI up but no HID keyboard\n");
        BootLog("Boot: XHCI up but no HID keyboard\n");
        if (gEnumWhy) {
            BootLog(gEnumWhy);
        }
        DebugWrite("XHCI: no keyboard\n");
        for (UINT32 P = 1; P <= gMaxPorts && P <= 32; P++) {
            if (InitMouseOnPort(P)) {
                BootLog("Boot: XHCI mouse only\n");
                break;
            }
        }
        gXhciStarted = 1;
        return 1;
    }

    DebugWrite("XHCI: keyboard ready\n");
    /* 与 mouse 同走 BootLog：真机屏上先 keyboard 再 mouse，再由 Probe 打 init returned */
    BootLog("Boot: XHCI-HID Keyboard\n");

    XhciEnumBindMouseAfterKbd();

    gXhciStarted = 1;
    return 1;
}
