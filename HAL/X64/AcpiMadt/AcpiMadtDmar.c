/*
 * AcpiMadtDmar.c — DMAR DRHD 关 TE（PR-S-acpimadt-1）
 */
#include "AcpiMadt.h"
#include "AcpiMadtPrivate.h"
#include "Hal.h"
#include "HalSerial.h"
#include "PCIe.h"
#include "ToySerialLog.h"
#include "VirtualMemory.h"
#include "Debug.h"


#define DMAR_GCMD 0x18u
#define DMAR_GSTS 0x1cu
#define DMA_GCMD_TE  (1u << 31)
#define DMA_GCMD_IRE (1u << 25)
#define DMA_GCMD_QIE (1u << 26)
#define DMA_GSTS_TES (1u << 31)
#define DMA_GSTS_IRES (1u << 25)
#define DMA_GSTS_QIES (1u << 26)
#define PTE_MMIO_VTD (PTE_PRESENT | PTE_WRITABLE | (1ULL << 3) | (1ULL << 4))

static int DrhdDisableTe(UINT64 RegBase) {
    volatile UINT32 *Gsts;
    volatile UINT32 *Gcmd;
    UINT32 Sts;
    UINT32 GcmdVal;
    int Wait;

    if (RegBase == 0) {
        return -1;
    }
    if (!VirtualMemoryEnabled()) {
        return -1;
    }
    if (VirtualMemoryMapRange(RegBase, RegBase, 0x1000, PTE_MMIO_VTD) != 0) {
        return -1;
    }
    Gsts = (volatile UINT32 *)(UINTN)(RegBase + DMAR_GSTS);
    Gcmd = (volatile UINT32 *)(UINTN)(RegBase + DMAR_GCMD);
    Sts = *Gsts;
    if (!(Sts & DMA_GSTS_TES)) {
        return 1; /* TE already off */
    }
    /* 与 Linux 一致：用 GSTS 镜像出 GCMD 影子，再清 TE */
    GcmdVal = 0;
    if (Sts & DMA_GSTS_TES) {
        GcmdVal |= DMA_GCMD_TE;
    }
    if (Sts & DMA_GSTS_IRES) {
        GcmdVal |= DMA_GCMD_IRE;
    }
    if (Sts & DMA_GSTS_QIES) {
        GcmdVal |= DMA_GCMD_QIE;
    }
    GcmdVal &= ~DMA_GCMD_TE;
    *Gcmd = GcmdVal;
    for (Wait = 0; Wait < 1000000; Wait++) {
        if (!(*Gsts & DMA_GSTS_TES)) {
            return 2; /* disabled */
        }
    }
    return -1;
}

int AcpiDmarDisableTranslation(UINT64 RsdpPhys) {
    ACPI_SDT_HEADER *Dmar;
    UINT8 *P;
    UINT8 *End;
    int SawDrhd = 0;
    int Disabled = 0;
    int AlreadyOff = 0;

    if (RsdpPhys == 0) {
        return -1;
    }
    Dmar = FindDmar(RsdpPhys);
    if (!Dmar) {
        return 0;
    }
    /* DMAR body: HostAddressWidth(1)+Flags(1)+Reserved(10) then structures */
    if (Dmar->Length < sizeof(ACPI_SDT_HEADER) + 12) {
        return -1;
    }
    P = (UINT8 *)Dmar + sizeof(ACPI_SDT_HEADER) + 12;
    End = (UINT8 *)Dmar + Dmar->Length;
    while (P + 4 <= End) {
        UINT16 Type = (UINT16)(P[0] | (P[1] << 8));
        UINT16 Len = (UINT16)(P[2] | (P[3] << 8));
        if (Len < 4 || P + Len > End) {
            break;
        }
        /* Type 0 = DRHD */
        if (Type == 0 && Len >= 16) {
            UINT64 RegBase = *(UINT64 *)(P + 8);
            int Rc = DrhdDisableTe(RegBase);
            SawDrhd = 1;
            if (Rc == 2) {
                Disabled = 1;
            } else if (Rc == 1) {
                AlreadyOff = 1;
            } else if (Rc < 0) {
                return -1;
            }
        }
        P += Len;
    }
    if (!SawDrhd) {
        return 0;
    }
    if (Disabled) {
        return 2;
    }
    if (AlreadyOff) {
        return 1;
    }
    return 1;
}
