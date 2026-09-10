/*
 * AcpiMadt.c — 从 RSDP 找到 MADT，枚举 Local APIC；查 DMAR 等表
 *
 * UEFI 真机：ACPI 表常在早期 identity 窗外，读前必须 MapRange。
 */
#include "AcpiMadt.h"
#include "Hal.h"
#include "PCIe.h"
#include "ToySerialLog.h"
#include "VirtualMemory.h"

#define SmpLog(Text) ToyLogSmp(Text)
#define SmpLogHex32(V) ToyLogSmpHex32(V)
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
#define PM1_PWRBTN_EN  (1u << 8)
#define PM1_SLP_EN     (1u << 13)
#define PM1_SCI_EN     (1u << 0)
/* FADT Flags bit4：1=电源键仅 control-method（无固定功能位） */
#define FADT_FLAG_PWR_BUTTON (1u << 4)
/* FADT Flags bit10：RESET_REG 有效 */
#define FADT_FLAG_RESET_REG  (1u << 10)

static UINT16 gPm1aEvt;
static UINT16 gPm1aCnt;
static UINT16 gPm1bEvt;
static UINT16 gPm1bCnt;
static UINT16 gPm1aEn;
static UINT16 gPm1bEn;
static UINT8  gPm1EvtLen;
static UINT8  gSlpTypA; /* 来自 _S5_；0xFF=未知 */
static UINT8  gPowerReady;
/* FADT RESET_REG：0=mem 1=io 2=pci；Addr=0 表示无 */
static UINT8  gResetSpace;
static UINT64 gResetAddr;
static UINT8  gResetValue;
static UINT8  gResetAccess; /* GAS AccessSize：1=byte 2=word 3=dword */

static UINT64 GasIoAddress(const UINT8 *Gas) {
    /* ACPI GAS：Address @ +4（旧误用 +8）；SpaceId 1=SystemIO */
    if (Gas[0] != 1) {
        return 0;
    }
    return *(UINT64 *)(void *)(Gas + 4);
}

static UINT64 GasAnyAddress(const UINT8 *Gas) {
    return *(UINT64 *)(void *)(Gas + 4);
}

static void PowerStallMs(UINT32 Ms) {
    UINT32 Lo;
    UINT32 Hi;
    UINT64 T0;
    UINT64 Need;
    UINT64 Now;

    __asm__ volatile("rdtsc" : "=a"(Lo), "=d"(Hi));
    T0 = ((UINT64)Hi << 32) | Lo;
    /* NUC ~3GHz；偏大无妨，仅用于等 SCI_EN */
    Need = (UINT64)Ms * 3000000ULL;
    for (;;) {
        __asm__ volatile("rdtsc" : "=a"(Lo), "=d"(Hi));
        Now = ((UINT64)Hi << 32) | Lo;
        if (Now - T0 >= Need) {
            break;
        }
        HalCpuRelax();
    }
}

static void PowerBootLine(const char *Text) {
    SmpLog(Text);
    HalSerialBootMark(Text);
}

static void PowerBootHex(const char *Prefix, UINT32 Value) {
    char Line[56];
    char Hex[12];
    int n = 0;
    int i;

    while (Prefix[n] != 0 && n < 36) {
        Line[n] = Prefix[n];
        n++;
    }
    HalSerialFormatHex(Hex, Value, 4);
    for (i = 0; Hex[i] != 0 && n < 52; i++) {
        Line[n++] = Hex[i];
    }
    Line[n++] = '\n';
    Line[n] = 0;
    PowerBootLine(Line);
}

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

/* AML：Name(_S5_, Package(){ TypA, ... }) → 取第一元素为 SLP_TYP */
static int ParseSlpTypFromAml(const UINT8 *Data, UINT32 Len, UINT8 *OutTyp) {
    UINT32 i;
    const UINT8 *P;
    UINT8 PkgLenByte;
    UINT8 Extra;

    if (Data == 0 || Len < 8 || OutTyp == 0) {
        return -1;
    }
    for (i = 0; i + 8 < Len; i++) {
        /* NameOp + "_S5_" */
        if (Data[i] != 0x08 || Data[i + 1] != '_' || Data[i + 2] != 'S' ||
            Data[i + 3] != '5' || Data[i + 4] != '_') {
            continue;
        }
        P = Data + i + 5;
        if (P >= Data + Len || *P != 0x12) { /* PackageOp */
            continue;
        }
        P++;
        if (P >= Data + Len) {
            continue;
        }
        PkgLenByte = *P;
        Extra = (UINT8)(PkgLenByte >> 6);
        if (Extra == 0) {
            P++;
        } else {
            if ((UINT32)(P - Data) + Extra + 1u >= Len) {
                continue;
            }
            P += Extra + 1;
        }
        if (P >= Data + Len) {
            continue;
        }
        P++; /* NumElements */
        if (P >= Data + Len) {
            continue;
        }
        if (*P == 0x0A && (P + 1) < Data + Len) { /* BytePrefix */
            *OutTyp = P[1] & 7u;
            return 0;
        }
        if (*P == 0x00) {
            *OutTyp = 0;
            return 0;
        }
        if (*P == 0x01) {
            *OutTyp = 1;
            return 0;
        }
        if (*P == 0x0B && (P + 2) < Data + Len) { /* WordPrefix */
            *OutTyp = P[1] & 7u;
            return 0;
        }
    }
    return -1;
}

static void ParseSlpTypFromFacp(UINT64 RsdpPhys, ACPI_SDT_HEADER *Facp, UINT8 *P) {
    UINT32 Dsdt32;
    UINT64 Dsdt64;
    ACPI_SDT_HEADER *Dsdt;
    ACPI_RSDP *Rsdp;
    ACPI_SDT_HEADER *Root;
    UINT8 Typ;
    UINT32 i;
    UINT32 Entries;

    gSlpTypA = 0xFF;
    Dsdt32 = *(UINT32 *)(void *)(P + 40);
    Dsdt64 = 0;
    if (Facp->Length >= 148) {
        Dsdt64 = *(UINT64 *)(void *)(P + 140); /* X_DSDT */
    }
    Dsdt = 0;
    if (Dsdt64 != 0) {
        Dsdt = MapSdtHeader(Dsdt64);
    }
    if (Dsdt == 0 && Dsdt32 != 0) {
        Dsdt = MapSdtHeader((UINT64)Dsdt32);
    }
    Typ = 0xFF;
    if (Dsdt != 0 &&
        ParseSlpTypFromAml((const UINT8 *)(UINTN)Dsdt, Dsdt->Length, &Typ) == 0) {
        gSlpTypA = Typ;
        PowerBootHex("boot: ACPI _S5_ typ=", gSlpTypA);
        return;
    }

    /* 部分固件把 _S5_ 只放在 SSDT */
    if (RsdpPhys == 0 || MapPhys(RsdpPhys, sizeof(ACPI_RSDP)) != 0) {
        return;
    }
    Rsdp = (ACPI_RSDP *)(UINTN)RsdpPhys;
    Root = 0;
    if (Rsdp->Revision >= 2 && Rsdp->XsdtAddress != 0) {
        Root = MapSdtHeader(Rsdp->XsdtAddress);
        if (Root) {
            Entries = (Root->Length - (UINT32)sizeof(ACPI_SDT_HEADER)) / 8u;
            if (Entries > ACPI_MAX_ROOT_ENTRIES) {
                Entries = ACPI_MAX_ROOT_ENTRIES;
            }
            for (i = 0; i < Entries; i++) {
                UINT64 Phys = ((UINT64 *)(void *)(Root + 1))[i];
                ACPI_SDT_HEADER *Tab = MapSdtHeader(Phys);
                if (Tab == 0 || !MemEq(Tab->Signature, "SSDT", 4)) {
                    continue;
                }
                if (ParseSlpTypFromAml((const UINT8 *)(UINTN)Tab, Tab->Length, &Typ) == 0) {
                    gSlpTypA = Typ;
                    PowerBootHex("boot: ACPI _S5_ typ=", gSlpTypA);
                    return;
                }
            }
        }
    }
}

static void Pm1EnablePowerButton(UINT16 EvtPort, UINT16 EnPort) {
    UINT16 En;

    if (EvtPort == 0 || EnPort == 0) {
        return;
    }
    En = HalIoRead16(EnPort);
    HalIoWrite16(EnPort, (UINT16)(En | PM1_PWRBTN_EN));
    HalIoWrite16(EvtPort, PM1_PWRBTN_STS); /* W1C 清残留 */
}

static void Pm1WriteSleep(UINT16 CntPort, UINT8 Typ) {
    UINT16 V;

    if (CntPort == 0) {
        return;
    }
    V = HalIoRead16(CntPort);
    V = (UINT16)((V & 0xC3FFu) | (((UINT16)Typ & 7u) << 10) | PM1_SLP_EN);
    HalIoWrite16(CntPort, V);
}

int AcpiPowerInit(UINT64 RsdpPhys) {
    ACPI_SDT_HEADER *Facp;
    UINT8 *P;
    UINT32 Pm1aEvt;
    UINT32 Pm1aCnt;
    UINT32 Pm1bEvt;
    UINT32 Pm1bCnt;
    UINT32 SmiCmd;
    UINT32 Flags;
    UINT32 LegEvt;
    UINT32 LegCnt;
    UINT32 LegEvtB;
    UINT32 LegCntB;
    UINT8 AcpiEnable;
    UINT16 Cnt;
    UINT32 Wait;
    UINT64 A;

    gPowerReady = 0;
    gPm1aEvt = 0;
    gPm1aCnt = 0;
    gPm1bEvt = 0;
    gPm1bCnt = 0;
    gPm1aEn = 0;
    gPm1bEn = 0;
    gPm1EvtLen = 4;
    gSlpTypA = 0xFF;
    gResetSpace = 0;
    gResetAddr = 0;
    gResetValue = 0;
    gResetAccess = 1;
    Facp = FindFacp(RsdpPhys);
    if (!Facp || Facp->Length < 116) {
        PowerBootLine("boot: ACPI no FACP\n");
        return -1;
    }
    if (MapPhys((UINT64)(UINTN)Facp, Facp->Length) != 0) {
        PowerBootLine("boot: ACPI FACP map fail\n");
        return -1;
    }
    P = (UINT8 *)Facp;
    /* ACPI 1.0 FADT：PM1a_EVT@56 PM1b@60 PM1a_CNT@64 PM1b_CNT@68 PM1_EVT_LEN@88 */
    Pm1aEvt = *(UINT32 *)(void *)(P + 56);
    Pm1bEvt = *(UINT32 *)(void *)(P + 60);
    Pm1aCnt = *(UINT32 *)(void *)(P + 64);
    Pm1bCnt = *(UINT32 *)(void *)(P + 68);
    gPm1EvtLen = P[88];
    if (gPm1EvtLen == 0) {
        gPm1EvtLen = 4;
    }
    SmiCmd = *(UINT32 *)(void *)(P + 48);
    AcpiEnable = P[52];
    Flags = *(UINT32 *)(void *)(P + 112);

    /*
     * ACPI 2.0+：优先 X_GAS（规范/Linux）；保留 legacy 以便 X 口无效时回退。
     * 旧逻辑「仅 32 位为 0 才读 X」在 NUC 上会用到废弃口 → 短按无 STS。
     */
    LegEvt = Pm1aEvt;
    LegCnt = Pm1aCnt;
    LegEvtB = Pm1bEvt;
    LegCntB = Pm1bCnt;
    if (Facp->Length >= 184) {
        A = GasIoAddress(P + 148); /* X_PM1a_EVT */
        if (A != 0 && A <= 0xFFFFu) {
            Pm1aEvt = (UINT32)A;
        }
        A = GasIoAddress(P + 160); /* X_PM1b_EVT */
        if (A != 0 && A <= 0xFFFFu) {
            Pm1bEvt = (UINT32)A;
        }
        A = GasIoAddress(P + 172); /* X_PM1a_CNT */
        if (A != 0 && A <= 0xFFFFu) {
            Pm1aCnt = (UINT32)A;
        }
        A = GasIoAddress(P + 184); /* X_PM1b_CNT */
        if (A != 0 && A <= 0xFFFFu) {
            Pm1bCnt = (UINT32)A;
        }
    }
    if (Pm1aCnt == 0 || Pm1aCnt > 0xFFFFu || Pm1aEvt == 0 || Pm1aEvt > 0xFFFFu) {
        /* X 无效则退回 legacy */
        Pm1aEvt = LegEvt;
        Pm1aCnt = LegCnt;
        Pm1bEvt = LegEvtB;
        Pm1bCnt = LegCntB;
    }
    if (Pm1aCnt == 0 || Pm1aCnt > 0xFFFFu || Pm1aEvt == 0 || Pm1aEvt > 0xFFFFu) {
        PowerBootLine("boot: ACPI power ports missing\n");
        return -1;
    }
    /* 选出的口若全 1（未解码），改试 legacy */
    if (HalIoRead16((UINT16)Pm1aCnt) == 0xFFFFu && LegCnt != 0 && LegCnt <= 0xFFFFu &&
        LegCnt != Pm1aCnt) {
        PowerBootLine("boot: ACPI X_GAS dead, use legacy\n");
        Pm1aEvt = LegEvt;
        Pm1aCnt = LegCnt;
        Pm1bEvt = LegEvtB;
        Pm1bCnt = LegCntB;
    }
    gPm1aEvt = (UINT16)Pm1aEvt;
    gPm1aCnt = (UINT16)Pm1aCnt;
    gPm1aEn = (UINT16)(gPm1aEvt + (gPm1EvtLen / 2));
    if (Pm1bEvt != 0 && Pm1bEvt <= 0xFFFFu) {
        gPm1bEvt = (UINT16)Pm1bEvt;
        gPm1bEn = (UINT16)(gPm1bEvt + (gPm1EvtLen / 2));
    }
    if (Pm1bCnt != 0 && Pm1bCnt <= 0xFFFFu) {
        gPm1bCnt = (UINT16)Pm1bCnt;
    }

    /*
     * 无 SCI_EN 时多数板卡不锁存 PWRBTN_STS；写 SMI_CMD(ACPI_ENABLE) 切入 ACPI 模式。
     * 真机 SMM 可能较慢，毫秒级等待（勿只 pause 空转）。
     */
    Cnt = HalIoRead16(gPm1aCnt);
    if ((Cnt & PM1_SCI_EN) == 0 && SmiCmd != 0 && SmiCmd <= 0xFFFFu && AcpiEnable != 0) {
        HalIoWrite8((UINT16)SmiCmd, AcpiEnable);
        for (Wait = 0; Wait < 50u; Wait++) {
            if (HalIoRead16(gPm1aCnt) & PM1_SCI_EN) {
                break;
            }
            PowerStallMs(1);
        }
    }
    Cnt = HalIoRead16(gPm1aCnt);
    if ((Cnt & PM1_SCI_EN) == 0) {
        /* 部分 PCH 允许直接置位；写了无效也无害 */
        HalIoWrite16(gPm1aCnt, (UINT16)(Cnt | PM1_SCI_EN));
        PowerStallMs(1);
    }

    Pm1EnablePowerButton(gPm1aEvt, gPm1aEn);
    Pm1EnablePowerButton(gPm1bEvt, gPm1bEn);
    ParseSlpTypFromFacp(RsdpPhys, Facp, P);

    /* ACPI 2.0+ RESET_REG（真机重启优先；无 flag 但口为 0xCF9 也收） */
    if (Facp->Length >= 129) {
        UINT8 Space = P[116];
        UINT64 Ra = GasAnyAddress(P + 116);
        UINT8 Val = P[128];
        int Want = 0;

        if (Flags & FADT_FLAG_RESET_REG) {
            Want = 1;
        } else if (Space == 1 && Ra == 0xCF9ull) {
            /* 部分固件漏 RESET_REG_SUP，但 GAS 已填 CF9 */
            Want = 1;
            PowerBootLine("boot: ACPI reset=cf9 (no flag)\n");
        }
        if (Want && (Space <= 2) && Ra != 0) {
            gResetSpace = Space;
            gResetAddr = Ra;
            gResetValue = Val ? Val : 0x06;
            gResetAccess = P[119];
            if (gResetAccess == 0) {
                gResetAccess = 1;
            }
            PowerBootHex("boot: ACPI reset space=", Space);
            PowerBootHex("boot: ACPI reset addr=", (UINT32)Ra);
            PowerBootHex("boot: ACPI reset val=", gResetValue);
        }
    }

    PowerBootHex("boot: ACPI PM1 evt=", gPm1aEvt);
    PowerBootHex("boot: ACPI PM1 cnt=", gPm1aCnt);
    PowerBootHex("boot: ACPI PM1 en=", gPm1aEn);
    PowerBootHex("boot: ACPI sci=", HalIoRead16(gPm1aCnt) & PM1_SCI_EN);
    PowerBootHex("boot: ACPI enrd=", HalIoRead16(gPm1aEn) & PM1_PWRBTN_EN);
    if (Flags & FADT_FLAG_PWR_BUTTON) {
        PowerBootLine("boot: ACPI pwrbtn=aml (still arm fixed)\n");
    } else {
        PowerBootLine("boot: ACPI pwrbtn=fixed\n");
    }

    gPowerReady = 1;
    return 0;
}

void AcpiPowerOff(void) {
    UINT8 Order[4];
    UINT8 N;
    UINT8 i;
    UINT8 Typ;
    UINT8 Seen[8];

    HalIrqDisable();

    /* QEMU/Bochs 常见关机口 */
    HalIoWrite16(0x604, 0x2000);
    HalIoWrite16(0xB004, 0x2000);
    HalIoWrite16(0x4004, 0x3400);

    if (!gPowerReady || gPm1aCnt == 0) {
        return;
    }

    /*
     * 只试 _S5_ 与常见 5/7。禁止扫 Typ0～7：错误 SLP_TYP 会把芯片打进
     * S1/S3 类「假死」（屏亮/灯亮但无响应），开始菜单关机看起来像卡死。
     */
    N = 0;
    for (i = 0; i < 8; i++) {
        Seen[i] = 0;
    }
    if (gSlpTypA != 0xFF) {
        Order[N++] = gSlpTypA & 7u;
        Seen[gSlpTypA & 7u] = 1;
    }
    if (!Seen[5]) {
        Order[N++] = 5;
        Seen[5] = 1;
    }
    if (!Seen[7]) {
        Order[N++] = 7;
        Seen[7] = 1;
    }

    PowerBootLine("boot: ACPI poweroff\n");
    for (i = 0; i < N; i++) {
        Typ = Order[i];
        Pm1WriteSleep(gPm1aCnt, Typ);
        Pm1WriteSleep(gPm1bCnt, Typ);
        PowerStallMs(50);
    }
}

/* Linux/Windows：CF9 需先 |2 再写复位码，单写 0x06 部分机挂死不复位 */
static void Cf9Pulse(UINT8 Code) {
    UINT8 Cf9;

    Cf9 = (UINT8)(HalIoRead8(0xCF9) & (UINT8)~Code);
    HalIoWrite8(0xCF9, (UINT8)(Cf9 | 0x02));
    PowerStallMs(1);
    HalIoWrite8(0xCF9, (UINT8)(Cf9 | Code));
    PowerStallMs(15);
}

static void ResetWriteIo(UINT16 Port, UINT8 Access, UINT8 Value) {
    if (Access >= 3) {
        HalIoWrite32(Port, (UINT32)Value);
    } else if (Access == 2) {
        HalIoWrite16(Port, (UINT16)Value);
    } else {
        HalIoWrite8(Port, Value);
    }
}

static void ResetWritePci(UINT64 Addr, UINT8 Value) {
    UINT8 Dev = (UINT8)((Addr >> 32) & 0xFFu);
    UINT8 Fn = (UINT8)((Addr >> 16) & 0xFFu);
    UINT8 Off = (UINT8)(Addr & 0xFFu);
    UINT32 Aligned = (UINT32)(Off & ~3u);
    UINT32 Shift = (UINT32)(Off & 3u) * 8u;
    UINT32 Cur = PciReadConfig(0, Dev, Fn, (UINT8)Aligned);
    Cur = (Cur & ~(0xFFu << Shift)) | ((UINT32)Value << Shift);
    PciWriteConfig(0, Dev, Fn, (UINT8)Aligned, Cur);
}

void AcpiReset(void) {
    int Pass;

    if (gResetAddr == 0) {
        return;
    }
    PowerBootLine("boot: ACPI reset\n");
    for (Pass = 0; Pass < 2; Pass++) {
        if (gResetSpace == 1) {
            /* SystemIO：CF9 用双写脉冲（与 Linux BOOT_CF9 一致） */
            if (gResetAddr == 0xCF9ull) {
                Cf9Pulse(gResetValue);
            } else if (gResetAddr <= 0xFFFFull) {
                ResetWriteIo((UINT16)gResetAddr, gResetAccess, gResetValue);
                PowerStallMs(15);
            }
        } else if (gResetSpace == 0) {
            if (MapPhys(gResetAddr, 8) == 0) {
                if (gResetAccess >= 3) {
                    *(volatile UINT32 *)(UINTN)gResetAddr = (UINT32)gResetValue;
                } else if (gResetAccess == 2) {
                    *(volatile UINT16 *)(UINTN)gResetAddr = (UINT16)gResetValue;
                } else {
                    *(volatile UINT8 *)(UINTN)gResetAddr = gResetValue;
                }
            }
            PowerStallMs(15);
        } else if (gResetSpace == 2) {
            ResetWritePci(gResetAddr, gResetValue);
            PowerStallMs(15);
        }
    }
}

/* 供 HalCpuReboot：无 FADT 时也走 Linux CF9 脉冲 */
void AcpiCf9Reset(UINT8 Code) {
    if (Code == 0) {
        Code = 0x06;
    }
    PowerBootLine("boot: CF9 reset\n");
    Cf9Pulse(Code);
}

int AcpiPowerButtonPressed(void) {
    UINT16 Sts;

    if (!gPowerReady || gPm1aEvt == 0) {
        return 0;
    }

    /* 固件偶发清 EN：每次轮询重新武装 */
    if (gPm1aEn != 0) {
        UINT16 En = HalIoRead16(gPm1aEn);
        if ((En & PM1_PWRBTN_EN) == 0) {
            HalIoWrite16(gPm1aEn, (UINT16)(En | PM1_PWRBTN_EN));
        }
    }
    if (gPm1bEn != 0) {
        UINT16 En = HalIoRead16(gPm1bEn);
        if ((En & PM1_PWRBTN_EN) == 0) {
            HalIoWrite16(gPm1bEn, (UINT16)(En | PM1_PWRBTN_EN));
        }
    }

    Sts = HalIoRead16(gPm1aEvt);
    if (Sts & PM1_PWRBTN_STS) {
        HalIoWrite16(gPm1aEvt, PM1_PWRBTN_STS);
        return 1;
    }
    if (gPm1bEvt != 0) {
        Sts = HalIoRead16(gPm1bEvt);
        if (Sts & PM1_PWRBTN_STS) {
            HalIoWrite16(gPm1bEvt, PM1_PWRBTN_STS);
            return 1;
        }
    }
    return 0;
}
