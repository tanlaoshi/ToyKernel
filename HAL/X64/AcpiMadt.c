/*
 * AcpiMadt.c — 从 RSDP 找到 MADT，枚举 Local APIC
 */
#include "AcpiMadt.h"
#include "Hal.h"

#define SmpLog(Text) HalDebugWrite(Text)

typedef struct {
    char   Signature[8];
    UINT8  Checksum;
    char   OemId[6];
    UINT8  Revision;
    UINT32 RsdtAddress;
    UINT32 Length;
    UINT64 XsdtAddress;
    UINT8  ExtendedChecksum;
    UINT8  Reserved[3];
} __attribute__((packed)) ACPI_RSDP;

typedef struct {
    char   Signature[4];
    UINT32 Length;
    UINT8  Revision;
    UINT8  Checksum;
    char   OemId[6];
    char   OemTableId[8];
    UINT32 OemRevision;
    UINT32 CreatorId;
    UINT32 CreatorRevision;
} __attribute__((packed)) ACPI_SDT_HEADER;

typedef struct {
    ACPI_SDT_HEADER Header;
    UINT32 LocalApicAddress;
    UINT32 Flags;
} __attribute__((packed)) ACPI_MADT;

static int MemEq(const char *A, const char *B, int N) {
    int i;
    for (i = 0; i < N; i++) {
        if (A[i] != B[i]) {
            return 0;
        }
    }
    return 1;
}

static ACPI_SDT_HEADER *FindTableXsdt(ACPI_SDT_HEADER *Xsdt, const char *Sig) {
    UINT32 Entries;
    UINT32 i;
    UINT64 *Ptr;

    if (Xsdt == 0 || Xsdt->Length < sizeof(ACPI_SDT_HEADER) + 8) {
        return 0;
    }
    Entries = (Xsdt->Length - sizeof(ACPI_SDT_HEADER)) / 8;
    Ptr = (UINT64 *)(Xsdt + 1);
    for (i = 0; i < Entries; i++) {
        ACPI_SDT_HEADER *H = (ACPI_SDT_HEADER *)(UINTN)Ptr[i];
        if (H && MemEq(H->Signature, Sig, 4)) {
            return H;
        }
    }
    return 0;
}

static ACPI_SDT_HEADER *FindTableRsdt(ACPI_SDT_HEADER *Rsdt, const char *Sig) {
    UINT32 Entries;
    UINT32 i;
    UINT32 *Ptr;

    if (Rsdt == 0 || Rsdt->Length < sizeof(ACPI_SDT_HEADER) + 4) {
        return 0;
    }
    Entries = (Rsdt->Length - sizeof(ACPI_SDT_HEADER)) / 4;
    Ptr = (UINT32 *)(Rsdt + 1);
    for (i = 0; i < Entries; i++) {
        ACPI_SDT_HEADER *H = (ACPI_SDT_HEADER *)(UINTN)(UINT64)Ptr[i];
        if (H && MemEq(H->Signature, Sig, 4)) {
            return H;
        }
    }
    return 0;
}

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
    Rsdp = (ACPI_RSDP *)(UINTN)RsdpPhys;
    if (!MemEq(Rsdp->Signature, "RSD PTR ", 8)) {
        SmpLog("smp: bad RSDP signature\n");
        return -1;
    }

    Madt = 0;
    if (Rsdp->Revision >= 2 && Rsdp->XsdtAddress != 0) {
        Root = (ACPI_SDT_HEADER *)(UINTN)Rsdp->XsdtAddress;
        Madt = (ACPI_MADT *)FindTableXsdt(Root, "APIC");
    }
    if (Madt == 0 && Rsdp->RsdtAddress != 0) {
        Root = (ACPI_SDT_HEADER *)(UINTN)(UINT64)Rsdp->RsdtAddress;
        Madt = (ACPI_MADT *)FindTableRsdt(Root, "APIC");
    }
    if (Madt == 0) {
        SmpLog("smp: MADT not found\n");
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
        SmpLog("smp: no enabled Local APICs\n");
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
