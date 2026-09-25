/*
 * EhciHw.c — 单控制器 handoff / 复位 / CCS（PR-H-ehci-1）
 */
#include "EhciPrivate.h"
#include "VirtualMemory.h"
#include "Hal.h"
#include "HalSerial.h"
#include "ToySerialLog.h"

EHCI_CTRL gEhci[EHCI_MAX_CTRL];
int gEhciCount;
int gEhciReady;
UINT32 gEhciCcsOr;

static int WaitClearOp(EHCI_CTRL *C, UINT32 Off, UINT32 Bit, int Spins) {
    while (Spins-- > 0) {
        if ((EhciR32(C->Op, Off) & Bit) == 0) {
            return 1;
        }
        HalCpuRelax();
    }
    return 0;
}

static int WaitSetOp(EHCI_CTRL *C, UINT32 Off, UINT32 Bit, int Spins) {
    while (Spins-- > 0) {
        if (EhciR32(C->Op, Off) & Bit) {
            return 1;
        }
        HalCpuRelax();
    }
    return 0;
}

static void TakeLegacyPci(EHCI_CTRL *C) {
    UINT32 Hcc;
    UINT8 Eecp;
    UINT32 Val;
    int Spin;

    Hcc = EhciR32(C->Cap, EHCI_HCCPARAMS);
    Eecp = (UINT8)EHCI_HCC_EECP(Hcc);
    if (Eecp < 0x40) {
        return;
    }
    Val = PciReadConfig(C->Pci.Bus, C->Pci.Device, C->Pci.Function, Eecp);
    PciWriteConfig(C->Pci.Bus, C->Pci.Device, C->Pci.Function, Eecp,
                   Val | EHCI_LEGSUP_OS);
    Spin = HalCpuIsHypervisor() ? 100000 : 200000;
    while (Spin-- > 0) {
        Val = PciReadConfig(C->Pci.Bus, C->Pci.Device, C->Pci.Function, Eecp);
        if ((Val & EHCI_LEGSUP_BIOS) == 0) {
            return;
        }
        HalCpuRelax();
    }
    ToyBootMarkUsb("Boot: EHCI LEGSUP BIOS stuck\n");
}

void EhciSurveyCcs(EHCI_CTRL *C) {
    UINT8 P;
    UINT32 Ps;
    UINT32 Mask = 0;

    for (P = 1; P <= C->NPorts && P <= 16; P++) {
        Ps = EhciR32(C->Op, EHCI_PORTSC(P));
        /* 写 PORTSC 时清 W1C（bit1/3/5），避免误清其它态 */
        if ((Ps & EHCI_PORT_PP) == 0) {
            EhciW32(C->Op, EHCI_PORTSC(P),
                    (Ps & ~(EHCI_PORT_CSC | EHCI_PORT_PEDC | EHCI_PORT_OCC)) |
                        EHCI_PORT_PP);
            {
                int W = 50000;
                while (W-- > 0) {
                    HalCpuRelax();
                }
            }
            Ps = EhciR32(C->Op, EHCI_PORTSC(P));
        }
        if (Ps & EHCI_PORT_CCS) {
            Mask |= (1u << (P - 1));
        }
    }
    C->CcsMask = Mask;
}

static void LogCtrl(EHCI_CTRL *C, int Index) {
    char Hex[20];
    char Line[88];
    int n = 0;
    const char *P;

    P = "Boot: EHCI#";
    while (*P && n < 16) {
        Line[n++] = *P++;
    }
    Line[n++] = (char)('0' + (Index % 10));
    P = " ";
    while (*P && n < 20) {
        Line[n++] = *P++;
    }
    /* HalSerialFormatHex → "0x.."；数字从 [2] 起 */
    HalSerialFormatHex(Hex, C->Pci.Bus, 2);
    Line[n++] = Hex[2];
    Line[n++] = Hex[3];
    Line[n++] = ':';
    HalSerialFormatHex(Hex, C->Pci.Device, 2);
    Line[n++] = Hex[2];
    Line[n++] = Hex[3];
    Line[n++] = '.';
    HalSerialFormatHex(Hex, C->Pci.Function, 1);
    Line[n++] = Hex[2];
    P = " ports=";
    while (*P && n < 48) {
        Line[n++] = *P++;
    }
    HalSerialFormatHex(Hex, C->NPorts, 2);
    Line[n++] = Hex[2];
    Line[n++] = Hex[3];
    P = " CCS=";
    while (*P && n < 58) {
        Line[n++] = *P++;
    }
    HalSerialFormatHex(Hex, C->CcsMask, 4);
    Line[n++] = Hex[2];
    Line[n++] = Hex[3];
    Line[n++] = Hex[4];
    Line[n++] = Hex[5];
    Line[n++] = '\n';
    Line[n] = 0;
    ToyBootMarkUsb(Line);
}

int EhciInitOne(EHCI_CTRL *C, int Index) {
    UINT64 Bar;
    UINT32 Hcs;
    UINT32 Cmd;
    UINT8 CapLen;
    int RealPc = !HalCpuIsHypervisor();

    Bar = C->Pci.BaseAddress;
    /* 低 4 位已由 PciScan 清；拒绝对 0 / 过低 */
    if (Bar == 0 || Bar < 0x1000ULL) {
        if (RealPc) {
            ToyBootMarkUsb("Boot: EHCI reject BAR\n");
        }
        return 0;
    }
    if (VirtualMemoryMapRange(Bar, Bar, EHCI_BAR_MAP,
                              PTE_PRESENT | PTE_WRITABLE) != 0) {
        if (RealPc) {
            ToyBootMarkUsb("Boot: EHCI map fail\n");
        }
        return 0;
    }
    C->Cap = (volatile UINT8 *)(UINTN)Bar;
    CapLen = (UINT8)(EhciR32(C->Cap, EHCI_CAPLENGTH) & 0xFFu);
    if (CapLen < 0x10 || CapLen == 0xFF) {
        if (RealPc) {
            ToyBootMarkUsb("Boot: EHCI bad CapLen\n");
        }
        return 0;
    }
    C->CapLen = CapLen;
    C->Op = C->Cap + CapLen;
    Hcs = EhciR32(C->Cap, EHCI_HCSPARAMS);
    C->NPorts = (UINT8)EHCI_HCS_N_PORTS(Hcs);
    if (C->NPorts == 0 || C->NPorts > 16) {
        C->NPorts = 8;
    }

    TakeLegacyPci(C);

    /* 停跑 → HCReset；超时不弃口（仍扫 CCS） */
    Cmd = EhciR32(C->Op, EHCI_USBCMD);
    Cmd &= ~EHCI_CMD_RS;
    EhciW32(C->Op, EHCI_USBCMD, Cmd);
    (void)WaitSetOp(C, EHCI_USBSTS, EHCI_STS_HCHALTED, 200000);

    EhciW32(C->Op, EHCI_USBCMD, EHCI_CMD_HCRESET);
    if (!WaitClearOp(C, EHCI_USBCMD, EHCI_CMD_HCRESET, 400000)) {
        if (RealPc) {
            ToyBootMarkUsb("Boot: EHCI reset timeout (CCS anyway)\n");
        }
    }

    /* 口路由到 EHCI（相对伴生 UHCI/OHCI） */
    EhciW32(C->Op, EHCI_CONFIGFLAG, 1u);
    {
        int W = 20000;
        while (W-- > 0) {
            HalCpuRelax();
        }
    }

    EhciSurveyCcs(C);
    C->Up = 1;
    LogCtrl(C, Index);
    return 1;
}
