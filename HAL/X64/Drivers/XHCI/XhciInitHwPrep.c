/*
 * XhciInitHwPrep.c — RealPc DMAR/TE + CAP/环快照（PR-F-xhci-2）
 */
#include "XHCI/XhciInternal.h"

void XhciInitHwRealPcDmar(void) {
    /*
     * 实测：白字最后停在 ports=0x12 且无 B10 黄字 → Present 在该行可能不返回。
     * 真机：一进 Init 就 mute，ports/探针全走 BootMark（直写帧缓冲）。
     */
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

/* 1 = CAP/基址就绪；0 = 失败（已按需 unmute） */
int XhciInitHwMapCap(UINT64 BaseAddress, int RealPc, char *B) {
    UINT32 Cap;
    UINT32 CapLength;

    gCapabilityBase = BaseAddress;
    Cap = ReadMmio32(gCapabilityBase);
    CapLength = Cap & 0xFF;
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
            if (MapXhciDma(gFwErstbaSave, XHCI_PAGE_SIZE) != 0) {
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
    return 1;
}

/* 1 = 控制器已跑；0 = 失败 */
int XhciInitHwStartCtrl(UINT32 MaxSlots, int RealPc) {
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
    } else if (!ResetController() || !StartController(MaxSlots)) {
        return 0;
    }
    return 1;
}

void XhciInitHwSurveyPorts(void) {
    UINT32 Surveyed = 0;
    for (UINT32 P = 1; P <= gMaxPorts && P <= 32; P++) {
        UINT32 Ps = ReadMmio32(gOperationalBase + PortReg(P));
        if (Ps & PORTSC_CCS) {
            Surveyed++;
            DebugWrite("XHCI: survey port ");
            DebugHex32(P);
            DebugWrite(" portsc=");
            DebugHex32(Ps);
            DebugWrite("\n");
        }
    }
    BootLogHex("Boot: XHCI CCS ports=", Surveyed, 2);
    if (Surveyed == 0) {
        EnumWhy("Boot: Why=no CCS\n");
    }
}
