/*
 * SmpBoot.c — MADT + INIT/SIPI 拉起 AP；PR-S2 每核 timer / CpuId / ticks
 */
#include "AcpiMadt.h"
#include "Hal.h"
#include "Platform.h"
#include "HalPort.h"
#include "Scheduler.h"

/* PR-S1/S2 验证日志始终走串口（不受 TOY_DEBUG 开关影响） */
#define SmpLog(Text)      HalDebugWrite(Text)
#define SmpLogHex32(V)    HalDebugHex32(V)
#define SmpLogHex64(V)    HalDebugHex64(V)

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

/* Ready 用魔数，避免软重启后 0x7E00 残留非 0 被误判为 AP 已就绪 */
#define SMP_READY_MAGIC  0x534D5052u  /* 'SMPR' */

typedef struct {
    UINT64 Cr3;
    UINT64 StackTop;
    UINT64 Entry;
    volatile UINT32 Ready;
    UINT32 LogicalCpu;
} SMP_BOOT_PARAM;

extern UINT8 _binary_SmpTramp_bin_start[];
extern UINT8 _binary_SmpTramp_bin_end[];

static UINT8 gApicIds[HAL_MAX_CPUS];
static int gCpuCount = 1;
static UINT8 gBspApicId;
static UINT8 gApStacks[HAL_MAX_CPUS][8192] __attribute__((aligned(16)));
static volatile UINT32 gApHelloCount;
static volatile UINT64 gCpuTicks[HAL_MAX_CPUS];

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

/*
 * INIT/SIPI：QEMU 上通常很快 Ready；宿主被其它 QEMU 占满时短轮询会误超时，
 * 迟到的 AP 再跑半初始化路径会三 fault → 整机复位（timeout 后无限重启）。
 */
#define SMP_INIT_GAP_LOOPS    400000u
#define SMP_SIPI_GAP_LOOPS    100000u
#define SMP_READY_POLL_MAX   2000000u

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

/* 超时后把跳板改成 cli;hlt，迟到 SIPI 也只会停住 */
static void NeutralizeTrampoline(void) {
    UINT8 *P = (UINT8 *)(UINTN)SMP_TRAMP_PHYS;
    SMP_BOOT_PARAM *Param = (SMP_BOOT_PARAM *)(UINTN)SMP_PARAM_PHYS;

    P[0] = 0xFAu; /* cli */
    P[1] = 0xF4u; /* hlt */
    P[2] = 0xEBu; /* jmp short */
    P[3] = 0xFCu; /* -4 */
    Param->Entry = 0;
    Param->Ready = 0;
    Param->StackTop = 0;
}

static void ParkAp(UINT8 ApicId) {
    NeutralizeTrampoline();
    LapicSendIpi(ApicId, 0x0000C500u);
    DelayLoops(SMP_INIT_GAP_LOOPS);
    LapicSendIpi(ApicId, 0x00008500u);
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

/* 保证 BSP 在 gApicIds[0]，便于 HalCpuId()==0 表示 BSP */
static void NormalizeBspFirst(UINT8 BspId, int Count) {
    int i;
    for (i = 0; i < Count; i++) {
        if (gApicIds[i] == BspId) {
            UINT8 Tmp = gApicIds[0];
            gApicIds[0] = gApicIds[i];
            gApicIds[i] = Tmp;
            return;
        }
    }
    if (Count < HAL_MAX_CPUS) {
        for (i = Count; i > 0; i--) {
            gApicIds[i] = gApicIds[i - 1];
        }
        gApicIds[0] = BspId;
        gCpuCount = Count + 1;
    }
}

/* AP 入口：ArchApInit + 本核 LAPIC timer，tick 空转（不进调度） */
void SmpApEntry(void) {
    SMP_BOOT_PARAM *Param = (SMP_BOOT_PARAM *)(UINTN)SMP_PARAM_PHYS;
    UINT32 Logical = Param->LogicalCpu;
    UINT8 Id = LapicGetId();

    if (Logical >= HAL_MAX_CPUS) {
        Logical = HAL_MAX_CPUS - 1;
    }

    ArchApInit(Logical);
    TimerStart();
    /* AP 验证用慢定时器，避免 QEMU 下高频 IRQ 拖死启动路径 */
    *(volatile UINT32 *)(UINTN)(LAPIC_BASE + 0x380) = 5000000u;

    SmpLog("smp: hello cpu=");
    SmpLogHex32(Logical);
    SmpLog(" apic=");
    SmpLogHex32(Id);
    SmpLog("\n");
    gApHelloCount++;
    __asm__ volatile ("" ::: "memory");
    Param->Ready = SMP_READY_MAGIC;

    __asm__ volatile ("sti" ::: "memory");
    /* 等 BSP SchedulerStart 后进入本核 idle（PR-S3） */
    while (!SchedulerIsOnline()) {
        __asm__ volatile ("pause");
    }
    SchedulerApStart();
    for (;;) {
        __asm__ volatile ("hlt");
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

static int StartOneAp(UINT8 ApicId, UINT32 LogicalCpu) {
    SMP_BOOT_PARAM *Param = (SMP_BOOT_PARAM *)(UINTN)SMP_PARAM_PHYS;
    UINTN TrampSize =
        (UINTN)(_binary_SmpTramp_bin_end - _binary_SmpTramp_bin_start);
    UINT64 Cr3;
    int Tries;

    if (TrampSize == 0 || TrampSize > 0x1000) {
        SmpLog("smp: bad trampoline size\n");
        return -1;
    }
    if (LogicalCpu >= HAL_MAX_CPUS) {
        return -1;
    }

    MemCopy((void *)(UINTN)SMP_TRAMP_PHYS, _binary_SmpTramp_bin_start, TrampSize);
    SetupTrampolineGdt();

    __asm__ volatile ("mov %%cr3, %0" : "=r"(Cr3));
    Param->Cr3 = Cr3;
    Param->StackTop =
        (UINT64)(UINTN)(gApStacks[LogicalCpu] + sizeof(gApStacks[LogicalCpu]));
    Param->Entry = (UINT64)(UINTN)SmpApEntry;
    Param->LogicalCpu = LogicalCpu;
    Param->Ready = 0;

    /* INIT assert (level) → deassert → SIPI（必要时再发一次） */
    LapicSendIpi(ApicId, 0x0000C500u);
    DelayLoops(SMP_INIT_GAP_LOOPS);
    LapicSendIpi(ApicId, 0x00008500u);
    DelayLoops(SMP_INIT_GAP_LOOPS);

    LapicSendIpi(ApicId, 0x00000600u | SMP_SIPI_VECTOR);
    Tries = (int)SMP_READY_POLL_MAX;
    while (Param->Ready != SMP_READY_MAGIC && Tries-- > 0) {
        __asm__ volatile ("pause");
    }
    if (Param->Ready != SMP_READY_MAGIC) {
        DelayLoops(SMP_SIPI_GAP_LOOPS);
        LapicSendIpi(ApicId, 0x00000600u | SMP_SIPI_VECTOR);
        Tries = (int)SMP_READY_POLL_MAX;
        while (Param->Ready != SMP_READY_MAGIC && Tries-- > 0) {
            __asm__ volatile ("pause");
        }
    }
    if (Param->Ready != SMP_READY_MAGIC) {
        SmpLog("smp: AP timeout apic=");
        SmpLogHex32(ApicId);
        SmpLog("\n");
        ParkAp(ApicId);
        return -1;
    }
    return 0;
}

int HalCpuCount(void) {
    return gCpuCount > 0 ? gCpuCount : 1;
}

UINT32 HalCpuId(void) {
    UINT8 Apic = LapicGetId();
    int i;

    if (Apic == gBspApicId) {
        return 0;
    }
    for (i = 1; i < gCpuCount && i < HAL_MAX_CPUS; i++) {
        if (gApicIds[i] == Apic) {
            return (UINT32)i;
        }
    }
    return 0;
}

int HalCpuIsBsp(void) {
    return LapicGetId() == gBspApicId;
}

void HalCpuTickInc(void) {
    UINT32 Id = HalCpuId();
    if (Id < HAL_MAX_CPUS) {
        gCpuTicks[Id]++;
    }
}

UINT64 HalCpuTicks(UINT32 Cpu) {
    if (Cpu >= HAL_MAX_CPUS) {
        return 0;
    }
    return gCpuTicks[Cpu];
}

int HalSmpStartAps(void) {
    UINT64 Rsdp = HalPlatformRsdp();
    int Count = 0;
    UINT8 BspFromMadt = 0;
    int i;
    int Started = 0;
    UINT8 BspId;
    UINT32 Logical;

    gCpuCount = 1;
    gApHelloCount = 0;
    for (i = 0; i < HAL_MAX_CPUS; i++) {
        gCpuTicks[i] = 0;
    }
    BspId = LapicGetId();
    gBspApicId = BspId;
    gApicIds[0] = BspId;

    if (Rsdp == 0) {
        SmpLog("smp: no RSDP (single CPU)\n");
        return 0;
    }
    if (AcpiMadtParse(Rsdp, gApicIds, HAL_MAX_CPUS, &Count, &BspFromMadt) != 0) {
        return 0;
    }
    gCpuCount = Count;
    NormalizeBspFirst(BspId, Count);
    Count = gCpuCount;

    SmpLog("smp: MADT cpus=");
    SmpLogHex32((UINT32)Count);
    SmpLog(" bsp_apic=");
    SmpLogHex32(BspId);
    SmpLog("\n");

    for (i = 1; i < Count; i++) {
        Logical = (UINT32)i;
        if (StartOneAp(gApicIds[i], Logical) == 0) {
            Started++;
        }
    }
    /* 只统计实际起来的核，避免调度器以为有幽灵 AP */
    gCpuCount = 1 + Started;

    SmpLog("smp: APs started=");
    SmpLogHex32((UINT32)Started);
    SmpLog(" hellos=");
    SmpLogHex32(gApHelloCount);
    SmpLog("\n");

    /*
     * PR-S2：Ready 已证明 AP 起来；此处只短等第一个 tick 方便串口日志。
     * 勿用超大 DelayLoops——QEMU TCG + -smp 下 BSP 易被饿死。
     */
    if (Started > 0) {
        int Wait;
        for (Wait = 0; Wait < 40; Wait++) {
            if (gCpuTicks[1] != 0) {
                break;
            }
            DelayLoops(20000);
        }
    }
    SmpLog("smp: ticks");
    for (i = 0; i < gCpuCount && i < HAL_MAX_CPUS; i++) {
        SmpLog(" cpu");
        SmpLogHex32((UINT32)i);
        SmpLog("=");
        SmpLogHex64(gCpuTicks[i]);
    }
    SmpLog("\n");
    if (Started == 0 && Count > 1) {
        SmpLog("smp: continue single-CPU (AP failed)\n");
    }
    return 0;
}
