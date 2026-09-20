/*
 * AcpiMadtTable.c — RSDP/XSDT 查找与映射（PR-S-acpimadt-1）
 */
#include "AcpiMadt.h"
#include "AcpiMadtPrivate.h"
#include "Hal.h"
#include "HalSerial.h"
#include "PCIe.h"
#include "ToySerialLog.h"
#include "VirtualMemory.h"
#include "Debug.h"

int MemEq(const char *A, const char *B, int N) {
    int i;
    for (i = 0; i < N; i++) {
        if (A[i] != B[i]) {
            return 0;
        }
    }
    return 1;
}

/* UEFI ACPI 表常在 identity 窗外：读前按需映页，失败则跳过 */
int MapPhys(UINT64 Phys, UINTN Bytes) {
    if (Phys == 0 || Bytes == 0 || Bytes > ACPI_MAX_TABLE_BYTES) {
        return -1;
    }
    if (!VirtualMemoryEnabled()) {
        return 0;
    }
    return VirtualMemoryMapRange(Phys, Phys, Bytes, ACPI_MAP_FLAGS);
}

ACPI_SDT_HEADER *MapSdtHeader(UINT64 Phys) {
    ACPI_SDT_HEADER *H;
    UINT32 Len;

    if (Phys == 0) {
        return 0;
    }
    if (MapPhys(Phys, sizeof(ACPI_SDT_HEADER)) != 0) {
        return 0;
    }
    H = (ACPI_SDT_HEADER *)(UINTN)Phys;
    Len = H->Length;
    if (Len < sizeof(ACPI_SDT_HEADER) || Len > ACPI_MAX_TABLE_BYTES) {
        return 0;
    }
    if (MapPhys(Phys, Len) != 0) {
        return 0;
    }
    return H;
}

ACPI_SDT_HEADER *FindTableXsdt(ACPI_SDT_HEADER *Xsdt, const char *Sig) {
    UINT32 Entries;
    UINT32 i;
    UINT64 *Ptr;

    if (Xsdt == 0 || Xsdt->Length < sizeof(ACPI_SDT_HEADER) + 8) {
        return 0;
    }
    Entries = (Xsdt->Length - sizeof(ACPI_SDT_HEADER)) / 8;
    if (Entries > ACPI_MAX_ROOT_ENTRIES) {
        Entries = ACPI_MAX_ROOT_ENTRIES;
    }
    Ptr = (UINT64 *)(Xsdt + 1);
    for (i = 0; i < Entries; i++) {
        ACPI_SDT_HEADER *H = MapSdtHeader(Ptr[i]);
        if (H && MemEq(H->Signature, Sig, 4)) {
            return H;
        }
    }
    return 0;
}

ACPI_SDT_HEADER *FindTableRsdt(ACPI_SDT_HEADER *Rsdt, const char *Sig) {
    UINT32 Entries;
    UINT32 i;
    UINT32 *Ptr;

    if (Rsdt == 0 || Rsdt->Length < sizeof(ACPI_SDT_HEADER) + 4) {
        return 0;
    }
    Entries = (Rsdt->Length - sizeof(ACPI_SDT_HEADER)) / 4;
    if (Entries > ACPI_MAX_ROOT_ENTRIES) {
        Entries = ACPI_MAX_ROOT_ENTRIES;
    }
    Ptr = (UINT32 *)(Rsdt + 1);
    for (i = 0; i < Entries; i++) {
        ACPI_SDT_HEADER *H = MapSdtHeader((UINT64)Ptr[i]);
        if (H && MemEq(H->Signature, Sig, 4)) {
            return H;
        }
    }
    return 0;
}

ACPI_MADT *FindMadt(UINT64 RsdpPhys) {
    ACPI_RSDP *Rsdp;
    ACPI_SDT_HEADER *Root;
    ACPI_MADT *Madt;

    if (MapPhys(RsdpPhys, sizeof(ACPI_RSDP)) != 0) {
        return 0;
    }
    Rsdp = (ACPI_RSDP *)(UINTN)RsdpPhys;
    if (!MemEq(Rsdp->Signature, "RSD PTR ", 8)) {
        return 0;
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
    return Madt;
}

ACPI_SDT_HEADER *FindDmar(UINT64 RsdpPhys) {
    ACPI_RSDP *Rsdp;
    ACPI_SDT_HEADER *Root;
    ACPI_SDT_HEADER *Tab;

    if (MapPhys(RsdpPhys, sizeof(ACPI_RSDP)) != 0) {
        return 0;
    }
    Rsdp = (ACPI_RSDP *)(UINTN)RsdpPhys;
    if (!MemEq(Rsdp->Signature, "RSD PTR ", 8)) {
        return 0;
    }
    Tab = 0;
    if (Rsdp->Revision >= 2 && Rsdp->XsdtAddress != 0) {
        Root = MapSdtHeader(Rsdp->XsdtAddress);
        if (Root) {
            Tab = FindTableXsdt(Root, "DMAR");
        }
    }
    if (Tab == 0 && Rsdp->RsdtAddress != 0) {
        Root = MapSdtHeader((UINT64)Rsdp->RsdtAddress);
        if (Root) {
            Tab = FindTableRsdt(Root, "DMAR");
        }
    }
    return Tab;
}

ACPI_SDT_HEADER *FindFacp(UINT64 RsdpPhys) {
    ACPI_RSDP *Rsdp;
    ACPI_SDT_HEADER *Root;
    ACPI_SDT_HEADER *Tab;

    if (RsdpPhys == 0 || MapPhys(RsdpPhys, sizeof(ACPI_RSDP)) != 0) {
        return 0;
    }
    Rsdp = (ACPI_RSDP *)(UINTN)RsdpPhys;
    if (!MemEq(Rsdp->Signature, "RSD PTR ", 8)) {
        return 0;
    }
    Tab = 0;
    if (Rsdp->Revision >= 2 && Rsdp->XsdtAddress != 0) {
        Root = MapSdtHeader(Rsdp->XsdtAddress);
        if (Root) {
            Tab = FindTableXsdt(Root, "FACP");
        }
    }
    if (Tab == 0 && Rsdp->RsdtAddress != 0) {
        Root = MapSdtHeader((UINT64)Rsdp->RsdtAddress);
        if (Root) {
            Tab = FindTableRsdt(Root, "FACP");
        }
    }
    return Tab;
}
