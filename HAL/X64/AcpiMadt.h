/*
 * AcpiMadt.h — 解析 ACPI MADT，收集 Local APIC ID
 */
#ifndef ACPI_MADT_H
#define ACPI_MADT_H

#include "BootTypes.h"

#define SMP_MAX_CPUS 8

int AcpiMadtParse(UINT64 RsdpPhys, UINT8 *ApicIds, int MaxCpus, int *OutCount,
                  UINT8 *OutBspApicId);

/* 查 XSDT/RSDT 是否含签名为 Sig4 的表（如 "DMAR"）；1=有 0=无 -1=RSDP 无效 */
int AcpiTablePresent(UINT64 RsdpPhys, const char *Sig4);

/*
 * 若存在 DMAR：对每个 DRHD 若 GSTS.TES=1 则清 GCMD.TE（Linux kexec 同类）。
 * 返回：0=无DMAR 1=DMAR且TE本关 2=已关TE -1=失败
 */
int AcpiDmarDisableTranslation(UINT64 RsdpPhys);

#endif
