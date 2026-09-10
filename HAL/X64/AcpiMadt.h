/*
 * AcpiMadt.h — 解析 ACPI MADT，收集 Local APIC ID
 */
#ifndef ACPI_MADT_H
#define ACPI_MADT_H

#include "BootTypes.h"

#define SMP_MAX_CPUS 8

int AcpiMadtParse(UINT64 RsdpPhys, UINT8 *ApicIds, int MaxCpus, int *OutCount,
                  UINT8 *OutBspApicId);

/* PR-H-ioapic：MADT Type1 I/O APIC + Type2 Interrupt Source Override */
typedef struct {
    UINT64 Address;
    UINT32 GsiBase;
    UINT8  Id;
} ACPI_IOAPIC_INFO;

typedef struct {
    UINT8  IsaIrq;
    UINT32 Gsi;
    UINT16 Flags; /* ACPI MPS INTI：极性 / 触发 */
} ACPI_ISO_ENTRY;

int AcpiMadtParseIo(UINT64 RsdpPhys,
                    ACPI_IOAPIC_INFO *OutIo, int MaxIo, int *OutIoCount,
                    ACPI_ISO_ENTRY *OutIso, int MaxIso, int *OutIsoCount);

/* 查 XSDT/RSDT 是否含签名为 Sig4 的表（如 "DMAR"）；1=有 0=无 -1=RSDP 无效 */
int AcpiTablePresent(UINT64 RsdpPhys, const char *Sig4);

/*
 * 若存在 DMAR：对每个 DRHD 若 GSTS.TES=1 则清 GCMD.TE（Linux kexec 同类）。
 * 返回：0=无DMAR 1=DMAR且TE本关 2=已关TE -1=失败
 */
int AcpiDmarDisableTranslation(UINT64 RsdpPhys);

/* FACP：软关机 + 电源键（短按） */
int AcpiPowerInit(UINT64 RsdpPhys);
void AcpiPowerOff(void);
/* 1=检测到电源键按下（已清状态位） */
int AcpiPowerButtonPressed(void);

#endif
