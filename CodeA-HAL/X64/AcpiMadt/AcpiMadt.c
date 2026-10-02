/*
 * AcpiMadt.c — MADT 枚举与表探测（PR-S-acpimadt-1）
 *
 * UEFI 真机：ACPI 表常在早期 identity 窗外，读前必须 MapRange。
 */
#include "AcpiMadt.h"
#include "AcpiMadtPrivate.h"
#include "Hal.h"
#include "HalSerial.h"
#include "PCIe.h"
#include "ToySerialLog.h"
#include "VirtualMemory.h"
#include "Debug.h"

#define SmpLog(Text) ToyLogSmp(Text)

int AcpiMadtParse(UINT64 RsdpPhys, UINT8 *ApicIds, int MaxCpus, int *OutCount,
                  UINT8 *OutBspApicId) {
    ACPI_RSDP *Rsdp;
    ACPI_SDT_HEADER *Root;
    ACPI_MADT *Madt;
    UINT8 *P;
    UINT8 *End;
    int Count = 0;

    if (OutCount) {
        *OutCount = 0;
    }
    if (RsdpPhys == 0 || ApicIds == 0 || MaxCpus <= 0) {
        return -1;
    }
    if (MapPhys(RsdpPhys, sizeof(ACPI_RSDP)) != 0) {
        return -1;
    }
    Rsdp = (ACPI_RSDP *)(UINTN)RsdpPhys;
    if (!MemEq(Rsdp->Signature, "RSD PTR ", 8)) {
        SmpLog("Smp: Bad RSDP Signature\n");
        return -1;
    }

    Madt = 0;
    if (Rsdp->Revision >= 2 && Rsdp->XsdtAddress != 0) {
        Root = MapSdtHeader(Rsdp->XsdtAddress);
        if (Root) {
            Madt = (ACPI_MADT *)FindTableXsdt(Root, "APIC");
        }
    }
    if (Madt == 0 && Rsdp->RsdtAddress != 0) {
        Root = MapSdtHeader((UINT64)Rsdp->RsdtAddress);
        if (Root) {
            Madt = (ACPI_MADT *)FindTableRsdt(Root, "APIC");
        }
    }
    if (Madt == 0) {
        SmpLog("Smp: MADT Not Found\n");
        return -1;
    }

    P = (UINT8 *)(Madt + 1);
    End = (UINT8 *)Madt + Madt->Header.Length;
    while (P + 2 <= End) {
        UINT8 Type = P[0];
        UINT8 Len = P[1];
        if (Len < 2 || P + Len > End) {
            break;
        }
        /* Type 0: Processor Local APIC */
        if (Type == 0 && Len >= 8) {
            UINT8 ApicId = P[3];
            UINT32 Flags = *(UINT32 *)(P + 4);
            if ((Flags & 1u) && Count < MaxCpus) {
                ApicIds[Count++] = ApicId;
            }
        }
        /* Type 9: Processor Local x2APIC（APIC ID 取低 8 位即可用于 xAPIC ICR） */
        if (Type == 9 && Len >= 16) {
            UINT32 ApicId32 = *(UINT32 *)(P + 8);
            UINT32 Flags = *(UINT32 *)(P + 4);
            if ((Flags & 1u) && Count < MaxCpus && ApicId32 < 256u) {
                ApicIds[Count++] = (UINT8)ApicId32;
            }
        }
        P += Len;
    }

    if (Count == 0) {
        SmpLog("Smp: No Enabled Local APICs\n");
        return -1;
    }
    if (OutBspApicId) {
        *OutBspApicId = ApicIds[0];
    }
    if (OutCount) {
        *OutCount = Count;
    }
    return 0;
}

int AcpiMadtParseIo(UINT64 RsdpPhys,
                    ACPI_IOAPIC_INFO *OutIo, int MaxIo, int *OutIoCount,
                    ACPI_ISO_ENTRY *OutIso, int MaxIso, int *OutIsoCount) {
    ACPI_MADT *Madt;
    UINT8 *P;
    UINT8 *End;
    int IoCount = 0;
    int IsoCount = 0;

    if (OutIoCount) {
        *OutIoCount = 0;
    }
    if (OutIsoCount) {
        *OutIsoCount = 0;
    }
    if (RsdpPhys == 0) {
        return -1;
    }
    Madt = FindMadt(RsdpPhys);
    if (Madt == 0) {
        return -1;
    }

    P = (UINT8 *)(Madt + 1);
    End = (UINT8 *)Madt + Madt->Header.Length;
    while (P + 2 <= End) {
        UINT8 Type = P[0];
        UINT8 Len = P[1];
        if (Len < 2 || P + Len > End) {
            break;
        }
        /* Type 1: I/O APIC */
        if (Type == 1 && Len >= 12 && OutIo && IoCount < MaxIo) {
            OutIo[IoCount].Id = P[2];
            OutIo[IoCount].Address = (UINT64)(*(UINT32 *)(P + 4));
            OutIo[IoCount].GsiBase = *(UINT32 *)(P + 8);
            IoCount++;
        }
        /* Type 2: Interrupt Source Override */
        if (Type == 2 && Len >= 10 && OutIso && IsoCount < MaxIso) {
            OutIso[IsoCount].IsaIrq = P[3];
            OutIso[IsoCount].Gsi = *(UINT32 *)(P + 4);
            OutIso[IsoCount].Flags = *(UINT16 *)(P + 8);
            IsoCount++;
        }
        P += Len;
    }

    if (OutIoCount) {
        *OutIoCount = IoCount;
    }
    if (OutIsoCount) {
        *OutIsoCount = IsoCount;
    }
    return 0;
}

int AcpiTablePresent(UINT64 RsdpPhys, const char *Sig4) {
    ACPI_RSDP *Rsdp;
    ACPI_SDT_HEADER *Root;
    ACPI_SDT_HEADER *Tab;

    if (RsdpPhys == 0 || Sig4 == 0) {
        return -1;
    }
    if (MapPhys(RsdpPhys, sizeof(ACPI_RSDP)) != 0) {
        return -1;
    }
    Rsdp = (ACPI_RSDP *)(UINTN)RsdpPhys;
    if (!MemEq(Rsdp->Signature, "RSD PTR ", 8)) {
        return -1;
    }
    Tab = 0;
    if (Rsdp->Revision >= 2 && Rsdp->XsdtAddress != 0) {
        Root = MapSdtHeader(Rsdp->XsdtAddress);
        if (Root) {
            Tab = FindTableXsdt(Root, Sig4);
        }
    }
    if (Tab == 0 && Rsdp->RsdtAddress != 0) {
        Root = MapSdtHeader((UINT64)Rsdp->RsdtAddress);
        if (Root) {
            Tab = FindTableRsdt(Root, Sig4);
        }
    }
    return Tab ? 1 : 0;
}
