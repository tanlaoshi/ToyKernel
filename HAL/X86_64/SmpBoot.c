/*
 * SmpBoot.c — MADT + INIT/SIPI 拉起 AP（PR-S1）
 */
#include "AcpiMadt.h"
#include "Hal.h"
#include "Platform.h"

/* PR-S1 验证日志始终走串口（不受 TOY_DEBUG 开关影响） */
#define SmpLog(Text)      HalDebugWrite(Text)
#define SmpLogHex32(V)    HalDebugHex32(V)

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
#define SMP_SIPI_VECTOR  0x08u   /* start @ 0x8000 */

typedef struct {
    UINT64 Cr3;
    UINT64 StackTop;
    UINT64 Entry;
    volatile UINT32 Ready;
} SMP_BOOT_PARAM;

extern UINT8 _binary_SmpTramp_bin_start[];
extern UINT8 _binary_SmpTramp_bin_end[];

static UINT8 gApicIds[SMP_MAX_CPUS];
static int gCpuCount = 1;
static UINT8 gBspApicId;
static UINT8 gApStacks[SMP_MAX_CPUS][8192] __attribute__((aligned(16)));
static volatile UINT32 gApHelloCount;

static inline UINT32 LapicRead(UINT32 Off) {
    return *(volatile UINT32 *)(UINTN)(LAPIC_BASE + Off);
}

static inline void LapicWrite(UINT32 Off, UINT32 Val) {
    *(volatile UINT32 *)(UINTN)(LAPIC_BASE + Off) = Val;
}

static UINT8 LapicGetId(void) {
    return (UINT8)(LapicRead(LAPIC_ID) >> 24);
}

static void DelayLoops(volatile UINT32 N) {
    while (N--) {
        __asm__ volatile ("pause");
    }
}

static void LapicWaitIcr(void) {
    while (LapicRead(LAPIC_ICR_LO) & (1u << 12)) {
        __asm__ volatile ("pause");
    }
}

static void LapicSendIpi(UINT8 ApicId, UINT32 Lo) {
    LapicWaitIcr();
    LapicWrite(LAPIC_ICR_HI, ((UINT32)ApicId) << 24);
    LapicWrite(LAPIC_ICR_LO, Lo);
    LapicWaitIcr();
}

static void MemCopy(void *Dst, const void *Src, UINTN Len) {
    UINT8 *D = (UINT8 *)Dst;
    const UINT8 *S = (const UINT8 *)Src;
    UINTN i;
    for (i = 0; i < Len; i++) {
        D[i] = S[i];
    }
}

static void MemZero(void *Dst, UINTN Len) {
    UINT8 *D = (UINT8 *)Dst;
    UINTN i;
    for (i = 0; i < Len; i++) {
        D[i] = 0;
    }
}

/* AP 入口：打印后置位 Ready，然后停车（PR-S1 不进调度） */
void SmpApEntry(void) {
    UINT8 Id = LapicGetId();

    LapicWrite(LAPIC_TPR, 0);
    LapicWrite(LAPIC_SVR, (1u << 8) | 0xFF);

    SmpLog("smp: hello cpu apic=");
    SmpLogHex32(Id);
    SmpLog("\n");
    gApHelloCount++;
    ((SMP_BOOT_PARAM *)(UINTN)SMP_PARAM_PHYS)->Ready = 1;

    for (;;) {
        __asm__ volatile ("cli; hlt");
    }
}

static void SetupTrampolineGdt(void) {
    UINT64 *Gdt = (UINT64 *)(UINTN)SMP_GDT_PHYS;
    UINT16 *Gdtr = (UINT16 *)(UINTN)SMP_GDTR_PHYS;

    MemZero(Gdt, 8 * sizeof(UINT64));
    Gdt[0] = 0;
    /* 0x08: 32-bit code */
    Gdt[1] = 0x00CF9A000000FFFFULL;
    /* 0x10: 32-bit data */
    Gdt[2] = 0x00CF92000000FFFFULL;
    /* 0x18: 64-bit code */
    Gdt[3] = 0x00AF9A000000FFFFULL;
    /* 0x20: 64-bit data */
    Gdt[4] = 0x00CF92000000FFFFULL;

    Gdtr[0] = (UINT16)(5 * 8 - 1);
    *(UINT32 *)(Gdtr + 1) = (UINT32)SMP_GDT_PHYS;
    *(UINT16 *)((UINT8 *)Gdtr + 6) = 0;
}

static int StartOneAp(UINT8 ApicId, int StackIndex) {
    SMP_BOOT_PARAM *Param = (SMP_BOOT_PARAM *)(UINTN)SMP_PARAM_PHYS;
    UINTN TrampSize =
        (UINTN)(_binary_SmpTramp_bin_end - _binary_SmpTramp_bin_start);
    UINT64 Cr3;
    int Tries;

    if (TrampSize == 0 || TrampSize > 0x1000) {
        SmpLog("smp: bad trampoline size\n");
        return -1;
    }

    MemCopy((void *)(UINTN)SMP_TRAMP_PHYS, _binary_SmpTramp_bin_start, TrampSize);
    SetupTrampolineGdt();

    __asm__ volatile ("mov %%cr3, %0" : "=r"(Cr3));
    Param->Cr3 = Cr3;
    Param->StackTop =
        (UINT64)(UINTN)(gApStacks[StackIndex] + sizeof(gApStacks[StackIndex]));
    Param->Entry = (UINT64)(UINTN)SmpApEntry;
    Param->Ready = 0;

    /* INIT assert (level) → deassert → SIPI×2（Intel MP 启动序列） */
    LapicSendIpi(ApicId, 0x0000C500u);
    DelayLoops(10000000);
    LapicSendIpi(ApicId, 0x00008500u);
    DelayLoops(10000000);

    LapicSendIpi(ApicId, 0x00000600u | SMP_SIPI_VECTOR);
    DelayLoops(2000000);
    LapicSendIpi(ApicId, 0x00000600u | SMP_SIPI_VECTOR);

    Tries = 1000000;
    while (Param->Ready == 0 && Tries-- > 0) {
        __asm__ volatile ("pause");
    }
    if (Param->Ready == 0) {
        SmpLog("smp: AP timeout apic=");
        SmpLogHex32(ApicId);
        SmpLog("\n");
        return -1;
    }
    return 0;
}

int HalCpuCount(void) {
    return gCpuCount > 0 ? gCpuCount : 1;
}

UINT32 HalCpuId(void) {
    return LapicGetId();
}

int HalSmpStartAps(void) {
    UINT64 Rsdp = HalPlatformRsdp();
    int Count = 0;
    UINT8 BspFromMadt = 0;
    int i;
    int Started = 0;
    UINT8 BspId;

    gCpuCount = 1;
    gApHelloCount = 0;
    BspId = LapicGetId();
    gBspApicId = BspId;

    if (Rsdp == 0) {
        SmpLog("smp: no RSDP (single CPU)\n");
        return 0;
    }
    if (AcpiMadtParse(Rsdp, gApicIds, SMP_MAX_CPUS, &Count, &BspFromMadt) != 0) {
        /* 不阻断启动：保持单核 */
        return 0;
    }
    gCpuCount = Count;
    SmpLog("smp: MADT cpus=");
    SmpLogHex32((UINT32)Count);
    SmpLog(" bsp_apic=");
    SmpLogHex32(BspId);
    SmpLog("\n");

    for (i = 0; i < Count; i++) {
        if (gApicIds[i] == BspId) {
            continue;
        }
        if (StartOneAp(gApicIds[i], Started + 1) == 0) {
            Started++;
        }
    }
    SmpLog("smp: APs started=");
    SmpLogHex32((UINT32)Started);
    SmpLog(" hellos=");
    SmpLogHex32(gApHelloCount);
    SmpLog("\n");
    return 0;
}
