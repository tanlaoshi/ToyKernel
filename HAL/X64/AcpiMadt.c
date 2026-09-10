/*
 * AcpiMadt.c — 从 RSDP 找到 MADT，枚举 Local APIC；查 DMAR 等表
 *
 * UEFI 真机：ACPI 表常在早期 identity 窗外，读前必须 MapRange。
 */
#include "AcpiMadt.h"
#include "Hal.h"
#include "VirtualMemory.h"

#define SmpLog(Text) HalDebugWrite(Text)
#define ACPI_MAP_FLAGS (PTE_PRESENT | PTE_WRITABLE)
#define ACPI_MAX_ROOT_ENTRIES 256u
#define ACPI_MAX_TABLE_BYTES  (256u * 1024u)

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

/* UEFI ACPI 表常在 identity 窗外：读前按需映页，失败则跳过 */
static int MapPhys(UINT64 Phys, UINTN Bytes) {
    if (Phys == 0 || Bytes == 0 || Bytes > ACPI_MAX_TABLE_BYTES) {
        return -1;
    }
    if (!VirtualMemoryEnabled()) {
        return 0;
    }
    return VirtualMemoryMapRange(Phys, Phys, Bytes, ACPI_MAP_FLAGS);
}

static ACPI_SDT_HEADER *MapSdtHeader(UINT64 Phys) {
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

static ACPI_SDT_HEADER *FindTableXsdt(ACPI_SDT_HEADER *Xsdt, const char *Sig) {
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

static ACPI_SDT_HEADER *FindTableRsdt(ACPI_SDT_HEADER *Rsdt, const char *Sig) {
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
        SmpLog("smp: bad RSDP signature\n");
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

static ACPI_MADT *FindMadt(UINT64 RsdpPhys) {
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

#define DMAR_GCMD 0x18u
#define DMAR_GSTS 0x1cu
#define DMA_GCMD_TE  (1u << 31)
#define DMA_GCMD_IRE (1u << 25)
#define DMA_GCMD_QIE (1u << 26)
#define DMA_GSTS_TES (1u << 31)
#define DMA_GSTS_IRES (1u << 25)
#define DMA_GSTS_QIES (1u << 26)
#define PTE_MMIO_VTD (PTE_PRESENT | PTE_WRITABLE | (1ULL << 3) | (1ULL << 4))

static ACPI_SDT_HEADER *FindDmar(UINT64 RsdpPhys) {
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

/* ---- FACP 电源：短按电源键 / 软关机 ---- */
#define PM1_PWRBTN_STS (1u << 8)
#define PM1_SLP_EN     (1u << 13)

static UINT16 gPm1aEvt;
static UINT16 gPm1aCnt;
static UINT8  gPm1EvtLen;
static UINT8  gPowerReady;

static ACPI_SDT_HEADER *FindFacp(UINT64 RsdpPhys) {
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

int AcpiPowerInit(UINT64 RsdpPhys) {
    ACPI_SDT_HEADER *Facp;
    UINT8 *P;
    UINT32 Pm1aEvt;
    UINT32 Pm1aCnt;

    gPowerReady = 0;
    gPm1aEvt = 0;
    gPm1aCnt = 0;
    gPm1EvtLen = 4;
    Facp = FindFacp(RsdpPhys);
    if (!Facp || Facp->Length < 116) {
        return -1;
    }
    if (MapPhys((UINT64)(UINTN)Facp, Facp->Length) != 0) {
        return -1;
    }
    P = (UINT8 *)Facp;
    /* ACPI 1.0 FADT：PM1a_EVT@56 PM1a_CNT@64 PM1_EVT_LEN@88 */
    Pm1aEvt = *(UINT32 *)(void *)(P + 56);
    Pm1aCnt = *(UINT32 *)(void *)(P + 64);
    gPm1EvtLen = P[88];
    if (gPm1EvtLen == 0) {
        gPm1EvtLen = 4;
    }
    /* ACPI 2.0+：若 32 位口为 0，试 X_PM1a_* GAS（Address_Space=1 I/O） */
    if ((Pm1aCnt == 0 || Pm1aEvt == 0) && Facp->Length >= 244) {
        /* X_PM1a_EVT_BLK @148, X_PM1a_CNT_BLK @160：GAS Address @ +8 */
        if (P[148] == 1 && Pm1aEvt == 0) {
            Pm1aEvt = (UINT32)(*(UINT64 *)(void *)(P + 156));
        }
        if (P[160] == 1 && Pm1aCnt == 0) {
            Pm1aCnt = (UINT32)(*(UINT64 *)(void *)(P + 168));
        }
    }
    if (Pm1aCnt == 0 || Pm1aCnt > 0xFFFFu || Pm1aEvt > 0xFFFFu) {
        return -1;
    }
    gPm1aEvt = (UINT16)Pm1aEvt;
    gPm1aCnt = (UINT16)Pm1aCnt;
    gPowerReady = 1;
    return 0;
}

void AcpiPowerOff(void) {
    UINT16 V;
    UINT8 Typ;

    /* QEMU/Bochs 常见关机口 */
    HalIoWrite16(0x604, 0x2000);
    HalIoWrite16(0xB004, 0x2000);
    HalIoWrite16(0x4004, 0x3400);

    if (!gPowerReady || gPm1aCnt == 0) {
        return;
    }
    /* 试 SLP_TYP 0..7；多数板卡 5 或 0 */
    for (Typ = 0; Typ < 8; Typ++) {
        V = HalIoRead16(gPm1aCnt);
        V = (UINT16)((V & 0xC3FFu) | ((UINT16)Typ << 10) | PM1_SLP_EN);
        HalIoWrite16(gPm1aCnt, V);
    }
}

int AcpiPowerButtonPressed(void) {
    UINT16 Sts;
    UINT16 Off;

    if (!gPowerReady || gPm1aEvt == 0) {
        return 0;
    }
    /* PM1 状态在块低半；长度常 4 → 状态 16bit @ base */
    Off = 0;
    Sts = HalIoRead16((UINT16)(gPm1aEvt + Off));
    if (Sts & PM1_PWRBTN_STS) {
        HalIoWrite16((UINT16)(gPm1aEvt + Off), PM1_PWRBTN_STS); /* W1C */
        return 1;
    }
    return 0;
}
