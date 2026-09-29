/*
 * SmpBootPrivate.h — SmpBoot / SmpBootStart 内部交接（PR-S3-smpboot-1）
 */
#ifndef SMP_BOOT_PRIVATE_H
#define SMP_BOOT_PRIVATE_H

#include "Hal.h"
#include "ToySerialLog.h"
#include "Arch.h"

#define SmpLog(Text)      ToyLogSmp(Text)
#define SmpLogHex32(V)    ToyLogSmpHex32(V)
#define SmpLogHex64(V)    ToyLogSmpHex64(V)

#define LAPIC_BASE       0xFEE00000ULL
#define LAPIC_ID         0x20
#define LAPIC_EOI        0xB0
#define LAPIC_SVR        0xF0
#define LAPIC_ICR_LO     0x300
#define LAPIC_ICR_HI     0x310
#define LAPIC_TPR        0x80

#define SMP_TRAMP_PHYS   0x8000ULL
#define SMP_PARAM_PHYS   0x7E00ULL
#define SMP_GDTR_PHYS    0x7EF0ULL
#define SMP_GDT_PHYS     0x7F00ULL
#define SMP_SIPI_VECTOR  0x08u

#define SMP_READY_MAGIC  0x534D5052u

#define SMP_INIT_GAP_LOOPS    400000u
#define SMP_SIPI_GAP_LOOPS    100000u
#define SMP_READY_POLL_MAX   2000000u

typedef struct {
    UINT64 Cr3;
    UINT64 StackTop;
    UINT64 Entry;
    volatile UINT32 Ready;
    UINT32 LogicalCpu;
} SMP_BOOT_PARAM;

extern UINT8 gApicIds[HAL_MAX_CPUS];
extern int gCpuCount;
extern UINT8 gBspApicId;
extern UINT8 gApStacks[HAL_MAX_CPUS][8192];
extern volatile UINT32 gApHelloCount;
extern volatile UINT64 gCpuTicks[HAL_MAX_CPUS];

extern UINT8 _binary_SmpTramp_bin_start[];
extern UINT8 _binary_SmpTramp_bin_end[];

void SmpApEntry(void);
UINT8 LapicGetId(void);
void DelayLoops(volatile UINT32 N);
int StartOneAp(UINT8 ApicId, UINT32 LogicalCpu);
void NormalizeBspFirst(UINT8 BspId, int Count);

#endif
