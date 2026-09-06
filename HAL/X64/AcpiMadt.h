/*
 * AcpiMadt.h — 解析 ACPI MADT，收集 Local APIC ID
 */
#ifndef ACPI_MADT_H
#define ACPI_MADT_H

#include "BootTypes.h"

#define SMP_MAX_CPUS 8

int AcpiMadtParse(UINT64 RsdpPhys, UINT8 *ApicIds, int MaxCpus, int *OutCount,
                  UINT8 *OutBspApicId);

#endif
