/*
 * IoApic.c — PR-H-ioapic：I/O APIC 初始化与中断路由
 *
 * 8259 已全掩；设备经 IOAPIC → LAPIC。MSI 仍优先；本文件供 INTx 回退。
 */
#include "IoApic.h"
#include "AcpiMadt.h"
#include "Hal.h"
#include "Platform.h"
#include "VirtualMemory.h"

#define IOAPIC_DEFAULT_PHYS 0xFEC00000ULL
#define IOAPIC_REGSEL       0x00
#define IOAPIC_WIN          0x10
#define IOAPIC_ID           0x00
#define IOAPIC_VER          0x01
#define IOAPIC_REDTBL       0x10

#define ACPI_ISO_MAX        32
#define ACPI_IOAPIC_MAX     4

static volatile UINT32 *gIoApic;
static UINT64 gIoApicPhys;
static UINT32 gGsiBase;
static UINT32 gMaxRedir;
static int gReady;

static ACPI_ISO_ENTRY gIso[ACPI_ISO_MAX];
static int gIsoCount;

static void MapMmioPage(UINT64 Phys) {
    if (!VirtualMemoryEnabled()) {
        return;
    }
    VirtualMemoryMapRange(Phys, Phys, 0x1000,
                          PTE_PRESENT | PTE_WRITABLE | (1ULL << 3) | (1ULL << 4));
}

static UINT32 IoApicRead(UINT32 Reg) {
    gIoApic[IOAPIC_REGSEL / 4] = Reg;
    return gIoApic[IOAPIC_WIN / 4];
}

static void IoApicWrite(UINT32 Reg, UINT32 Val) {
    gIoApic[IOAPIC_REGSEL / 4] = Reg;
    gIoApic[IOAPIC_WIN / 4] = Val;
}

static void WriteRte(UINT32 Index, UINT32 Lo, UINT32 Hi) {
    IoApicWrite(IOAPIC_REDTBL + Index * 2, Lo);
    IoApicWrite(IOAPIC_REDTBL + Index * 2 + 1, Hi);
}

static int FindIso(UINT8 IsaIrq, ACPI_ISO_ENTRY *Out) {
    int i;
    for (i = 0; i < gIsoCount; i++) {
        if (gIso[i].IsaIrq == IsaIrq) {
            if (Out) {
                *Out = gIso[i];
            }
            return 1;
        }
    }
    return 0;
}

int IoApicReady(void) {
    return gReady;
}

int IoApicInit(void) {
    ACPI_IOAPIC_INFO Io[ACPI_IOAPIC_MAX];
    int IoCount = 0;
    UINT64 Rsdp;
    UINT32 Ver;
    UINT32 i;
    UINT64 Phys = IOAPIC_DEFAULT_PHYS;
    UINT32 GsiBase = 0;

    gReady = 0;
    gIsoCount = 0;
    gIoApic = 0;

    Rsdp = HalPlatformRsdp();
    if (Rsdp != 0 &&
        AcpiMadtParseIo(Rsdp, Io, ACPI_IOAPIC_MAX, &IoCount,
                        gIso, ACPI_ISO_MAX, &gIsoCount) == 0 &&
        IoCount > 0 && Io[0].Address != 0) {
        Phys = Io[0].Address;
        GsiBase = Io[0].GsiBase;
    }

    MapMmioPage(Phys);
    gIoApicPhys = Phys;
    gGsiBase = GsiBase;
    gIoApic = (volatile UINT32 *)(UINTN)Phys;

    Ver = IoApicRead(IOAPIC_VER);
    gMaxRedir = ((Ver >> 16) & 0xFFu) + 1u;
    if (gMaxRedir == 0 || gMaxRedir > 256u) {
        gMaxRedir = 24;
    }

    for (i = 0; i < gMaxRedir; i++) {
        WriteRte(i, 1u << 16, 0); /* mask */
    }

    gReady = 1;
    HalDebugWrite("boot: ioapic base=");
    HalDebugHex64(Phys);
    HalDebugWrite(" maxredir=");
    HalDebugWriteHex32(gMaxRedir);
    HalDebugWrite(" gsi0=");
    HalDebugWriteHex32(GsiBase);
    HalDebugWrite(" iso=");
    HalDebugWriteHex32((UINT32)gIsoCount);
    HalDebugWrite("\n");
    return 0;
}

void IoApicMaskGsi(UINT32 Gsi, int Mask) {
    UINT32 Index;
    UINT32 Lo;

    if (!gReady || Gsi < gGsiBase) {
        return;
    }
    Index = Gsi - gGsiBase;
    if (Index >= gMaxRedir) {
        return;
    }
    Lo = IoApicRead(IOAPIC_REDTBL + Index * 2);
    if (Mask) {
        Lo |= (1u << 16);
    } else {
        Lo &= ~(1u << 16);
    }
    IoApicWrite(IOAPIC_REDTBL + Index * 2, Lo);
}

int IoApicRouteGsi(UINT32 Gsi, UINT8 Vector, UINT8 DestApicId,
                   int Level, int ActiveLow) {
    UINT32 Index;
    UINT32 Lo;
    UINT32 Hi;

    if (!gReady || Vector < 0x20 || Gsi < gGsiBase) {
        return -1;
    }
    Index = Gsi - gGsiBase;
    if (Index >= gMaxRedir) {
        return -1;
    }

    Lo = (UINT32)Vector;
    if (ActiveLow) {
        Lo |= (1u << 13);
    }
    if (Level) {
        Lo |= (1u << 15);
    }
    /* bit16 clear = unmasked */
    Hi = ((UINT32)DestApicId) << 24;
    WriteRte(Index, Lo, Hi);

    HalDebugWrite("boot: ioapic route gsi=");
    HalDebugWriteHex32(Gsi);
    HalDebugWrite(" vec=");
    HalDebugWriteHex32(Vector);
    HalDebugWrite(" dest=");
    HalDebugWriteHex32(DestApicId);
    HalDebugWrite(Level ? " level" : " edge");
    HalDebugWrite(ActiveLow ? " low\n" : " high\n");
    return 0;
}

int IoApicRouteIsaIrq(UINT8 IsaIrq, UINT8 Vector, UINT8 DestApicId) {
    ACPI_ISO_ENTRY Iso;
    UINT32 Gsi = (UINT32)IsaIrq;
    int Level = 0;
    int ActiveLow = 0;
    UINT16 Flags;

    if (FindIso(IsaIrq, &Iso)) {
        Gsi = Iso.Gsi;
        Flags = Iso.Flags;
        /* polarity: 01=high 11=low；00=ISA 默认高 */
        if ((Flags & 3u) == 3u) {
            ActiveLow = 1;
        }
        /* trigger: 01=edge 11=level；00=ISA 默认边沿 */
        if (((Flags >> 2) & 3u) == 3u) {
            Level = 1;
        }
    }
    return IoApicRouteGsi(Gsi, Vector, DestApicId, Level, ActiveLow);
}

int IoApicRoutePciIntx(USB_CONTROLLER *Device, UINT8 Vector, UINT8 DestApicId) {
    UINT32 Dw;
    UINT8 Line;
    ACPI_ISO_ENTRY Iso;
    UINT32 Gsi;
    int Level = 1;
    int ActiveLow = 1;
    UINT16 Flags;
    UINT32 Cmd;

    if (!Device || !gReady) {
        return -1;
    }

    Dw = PciReadConfig(Device->Bus, Device->Device, Device->Function, 0x3C);
    Line = (UINT8)(Dw & 0xFFu);
    if (Line == 0 || Line == 0xFF || Line > 23) {
        HalDebugWrite("boot: ioapic pci intx: no line\n");
        return -1;
    }

    /* 允许 INTx：清 Interrupt Disable（cmd bit10） */
    Cmd = PciReadConfig(Device->Bus, Device->Device, Device->Function, 0x04);
    Cmd &= ~(1u << 10);
    PciWriteConfig(Device->Bus, Device->Device, Device->Function, 0x04, Cmd);

    Gsi = (UINT32)Line;
    if (FindIso(Line, &Iso)) {
        Gsi = Iso.Gsi;
        Flags = Iso.Flags;
        if ((Flags & 3u) == 1u) {
            ActiveLow = 0;
        } else if ((Flags & 3u) == 3u) {
            ActiveLow = 1;
        }
        if (((Flags >> 2) & 3u) == 1u) {
            Level = 0;
        } else if (((Flags >> 2) & 3u) == 3u) {
            Level = 1;
        }
    }

    return IoApicRouteGsi(Gsi, Vector, DestApicId, Level, ActiveLow);
}
