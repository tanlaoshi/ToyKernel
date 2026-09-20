/*
 * XhciInitHw.c — PR-S-xhci-1：BAR/DMAR/Halt/Start/端口勘察
 *
 * 从 XhciInit 原样抽出；不改语义。BSS 仍在 Xhci.c。
 */
#include "XHCI/XhciInternal.h"

int XhciInitHw(UINT64 BaseAddress) {
    int RealPc = !HalCpuIsHypervisor();
    char B[12];

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

    /*
     * 实测：白字最后停在 ports=0x12 且无 B10 黄字 → Present 在该行可能不返回。
     * 真机：一进 Init 就 mute，ports/探针全走 BootMark（直写帧缓冲）。
     */
    if (RealPc) {
        HalSerialGopMute(1);
        ToyBootMarkUsb("Boot: XHCI-HHID Enter\n");
        gXhciDmar = -2;
        gXhciTe = -2;
        {
            UINT64 Rsdp = HalPlatformRsdp();
            int Dmar;
            int Te;
            if (Rsdp == 0) {
                ToyBootMarkUsb("Boot: XHCI RSDP=0\n");
            } else {
                BootMarkV("Boot: XHCI RSDP ok\n");
                Dmar = AcpiTablePresent(Rsdp, "DMAR");
                gXhciDmar = Dmar;
                if (Dmar > 0) {
                    BootMarkV("Boot: XHCI DMAR=yes\n");
                    BootMarkV("Boot: XHCI TE off...\n");
                    Te = AcpiDmarDisableTranslation(Rsdp);
                    gXhciTe = Te;
                    if (Te == 2) {
                        BootMarkV("Boot: XHCI TE was ON->off\n");
                    } else if (Te == 1) {
                        BootMarkV("Boot: XHCI TE already off\n");
                    } else if (Te == 0) {
                        BootMarkV("Boot: XHCI TE no DRHD\n");
                    } else {
                        ToyBootMarkUsb("Boot: XHCI TE off fail\n");
                    }
                } else if (Dmar == 0) {
                    BootMarkV("Boot: XHCI DMAR=no\n");
                    gXhciTe = -2;
                } else {
                    ToyBootMarkUsb("Boot: XHCI DMAR=bad\n");
                }
            }
        }
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

    gCapabilityBase = BaseAddress;
    UINT32 Cap = ReadMmio32(gCapabilityBase);
    UINT32 CapLength = Cap & 0xFF;
    DiagChk("ReadCap", Cap != 0xFFFFFFFFu && CapLength >= 0x20 && CapLength != 0xFF,
            "CAP!=F.. len>=20", Cap, 8);
    if (Cap == 0xFFFFFFFFu || CapLength < 0x20 || CapLength == 0xFF) {
        if (RealPc) {
            ToyBootMarkUsb("Boot: XHCI bad CAP\n");
            HalSerialGopMute(0);
        } else {
            ToyLogUsb("Boot: XHCI bad CAP=");
            HalSerialFormatHex(B, Cap, 8);
            ToyLogUsb(B);
            ToyLogUsb("\n");
        }
        return 0;
    }

    gOperationalBase = gCapabilityBase + CapLength;
    gDoorbellBase = gCapabilityBase + (ReadMmio32(gCapabilityBase + 0x14) & ~0x3u);
    gRuntimeBase = gCapabilityBase + (ReadMmio32(gCapabilityBase + 0x18) & ~0x1Fu);
    gCtxSize = (ReadMmio32(gCapabilityBase + 0x10) & (1u << 2)) ? 64 : 32;

    /* 真机：Halt/TakeLegacy 前冻结固件环指针（其后 CRCR 常读成 0） */
    gFwDcbaapSave = 0;
    gFwCrcrSave = 0;
    gFwCrcrRcs = 1;
    gFwErstbaSave = 0;
    gFwEvtSave = 0;
    gFwEvtSegSave = 0;
    gFwErdpSave = 0;
    if (RealPc) {
        UINT8 *Erst;
        UINT64 Crcr;
        gFwDcbaapSave = ReadMmio64(gOperationalBase + 0x30) & ~0x3FULL;
        Crcr = ReadMmio64(gOperationalBase + 0x18);
        gFwCrcrSave = Crcr & ~0x3FULL;
        gFwCrcrRcs = (UINT32)(Crcr & 1ULL);
        gFwErstbaSave = ReadMmio64(gRuntimeBase + 0x30) & ~0x3FULL;
        gFwErdpSave = ReadMmio64(gRuntimeBase + 0x38);
        if (gFwErstbaSave != 0) {
            if (MapXhciDma(gFwErstbaSave, 0x1000) != 0) {
                ToyBootMarkUsb("Boot: XHCI map ERST fail\n");
            } else {
                Erst = (UINT8 *)(UINTN)gFwErstbaSave;
                gFwEvtSave = *(UINT64 *)(void *)Erst;
                gFwEvtSegSave = *(UINT16 *)(void *)(Erst + 8);
            }
        }
        if (gFwCrcrSave == 0 || gFwErstbaSave == 0) {
            BootMarkV("Boot: XHCI snap ring=0\n");
        } else {
            BootMarkV("Boot: XHCI snap rings ok\n");
        }
    }

    UINT32 Hcs1 = ReadMmio32(gCapabilityBase + 0x04);
    UINT32 MaxSlots = Hcs1 & 0xFF;
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
            int n = 0;
            const char *P = "Boot: XHCI ports=";
            while (*P && n < 28) {
                Msg[n++] = *P++;
            }
            Msg[n++] = B[0];
            Msg[n++] = B[1];
            Msg[n++] = '\n';
            Msg[n] = 0;
            ToyBootMarkUsb(Msg);
        } else {
            ToyLogUsb("Boot: XHCI ports=");
            ToyLogUsb(B);
            ToyLogUsb("\n");
        }
    }

    /*
     * PR-H-hub：真机不再 B14 裸 RS 后 return；HaltOnly（避免 HCRST）→ Start → 枚举。
     * 失败则 unmute，让 PS/2 有机会 Probe。
     */
    BootLogV("Boot: XHCI take legacy...\n");
    TakeLegacy();
    BootLogV("Boot: XHCI after legacy\n");

    if (RealPc) {
        BootMarkV("Boot: XHCI-HHID Halt\n");
        if (!HaltOnly()) {
            ToyBootMarkUsb("Boot: XHCI halt fail\n");
            HalSerialGopMute(0);
            ToyLogUsb("Boot: XHCI halt fail, desktop\n");
            return 0;
        }
        if (!StartController(MaxSlots)) {
            ToyBootMarkUsb("Boot: XHCI start fail\n");
            HalSerialGopMute(0);
            ToyLogUsb("Boot: XHCI start fail, desktop\n");
            HaltControllerQuiet();
            return 0;
        }
    } else {
        if (!ResetController() || !StartController(MaxSlots)) {
            return 0;
        }
    }
    BootLogV("Boot: XHCI controller running\n");
    DebugWrite("XHCI: controller running\n");
    PowerConnectedPorts();

    {
        UINT32 Surveyed = 0;
        for (UINT32 p = 1; p <= gMaxPorts && p <= 32; p++) {
            UINT32 Ps = ReadMmio32(gOperationalBase + PortReg(p));
            if (Ps & PORTSC_CCS) {
                Surveyed++;
                DebugWrite("XHCI: survey port ");
                DebugHex32(p);
                DebugWrite(" portsc=");
                DebugHex32(Ps);
                DebugWrite("\n");
            }
        }
        {
            BootLogHex("Boot: XHCI CCS ports=", Surveyed, 2);
            if (Surveyed == 0) {
                EnumWhy("Boot: Why=no CCS\n");
            }
        }
    }
    return 1;
}
