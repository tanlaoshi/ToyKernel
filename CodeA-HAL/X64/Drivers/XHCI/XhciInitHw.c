/*
 * XhciInitHw.c — PR-F-xhci-2：BAR/DMAR/Halt/Start/端口勘察（编排）
 *
 * Prep/Map/Start/Survey 见 XhciInitHwPrep.c。BSS 仍在 Xhci.c。
 */
#include "XHCI/XhciInternal.h"

int XhciInitHw(UINT64 BaseAddress) {
    int RealPc = !HalCpuIsHypervisor();
    char B[12];
    UINT32 Hcs1;
    UINT32 MaxSlots;

    if (gXhciStarted) {
        if (XhciHidKeyboardReady() || XhciMousePresent()) {
            ToyLogUsb("Boot: XHCI init skipped (already up)\n");
            return 1;
        }
        /* 控制器曾起但无 HID：勿假成功，否则 Probe/fallback 会挡住 PS/2 */
        ToyLogUsb("Boot: XHCI already up, no HID\n");
        return 0;
    }

    /* 运行时误调 / 损坏指针：QEMU 曾见 BAR=0x193A50 → Cap=0 后异常 */
    if (BaseAddress < 0x100000ULL || (BaseAddress & 0xFULL) != 0) {
        ToyLogUsb("Boot: XHCI reject BAR\n");
        return 0;
    }

    if (DiagVerbose()) {
        BootLog("xhci diag: VERBOSE\n");
    }
    /* 刷机核对：没有这行 = NUC 仍在跑旧 Kernel.elf */
    BootLogV("Boot: XHCI build=kbd-v8\n");
    gCtrlFailLogged = 0;

    if (RealPc) {
        XhciInitHwRealPcDmar();
    }

    if (BaseAddress == 0) {
        if (RealPc) {
            ToyBootMarkUsb("Boot: XHCI null BAR\n");
            HalSerialGopMute(0);
        } else {
            ToyLogUsb("Boot: XHCI null BAR\n");
        }
        return 0;
    }

    if (!XhciInitHwMapCap(BaseAddress, RealPc, B)) {
        return 0;
    }

    Hcs1 = ReadMmio32(gCapabilityBase + 0x04);
    MaxSlots = Hcs1 & 0xFF;
    gMaxPorts = (Hcs1 >> 24) & 0xFF;
    if (MaxSlots == 0) {
        MaxSlots = 1;
    }
    if (MaxSlots > DCBAA_SLOTS) {
        MaxSlots = DCBAA_SLOTS;
    }

    HalSerialFormatHex(B, gMaxPorts, 2);
    if (DiagVerbose()) {
        if (RealPc) {
            char Msg[40];
            int N = 0;
            const char *P = "Boot: XHCI ports=";
            while (*P && N < 28) {
                Msg[N++] = *P++;
            }
            Msg[N++] = B[0];
            Msg[N++] = B[1];
            Msg[N++] = '\n';
            Msg[N] = 0;
            ToyBootMarkUsb(Msg);
        } else {
            ToyLogUsb("Boot: XHCI ports=");
            ToyLogUsb(B);
            ToyLogUsb("\n");
        }
    }

    if (!XhciInitHwStartCtrl(MaxSlots, RealPc)) {
        return 0;
    }
    BootLogV("Boot: XHCI controller running\n");
    DebugWrite("XHCI: controller running\n");
    PowerConnectedPorts();
    XhciInitHwSurveyPorts();
    return 1;
}
