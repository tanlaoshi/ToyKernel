/*
 * SmpBootStart.c — INIT/SIPI 跳板与 StartOneAp（PR-S3-smpboot-1）
 */
#include "SmpBootPrivate.h"

static inline UINT32 LapicRead(UINT32 Off) {
    return *(volatile UINT32 *)(UINTN)(LAPIC_BASE + Off);
}

static inline void LapicWrite(UINT32 Off, UINT32 Val) {
    *(volatile UINT32 *)(UINTN)(LAPIC_BASE + Off) = Val;
}

UINT8 LapicGetId(void) {
    return (UINT8)(LapicRead(LAPIC_ID) >> 24);
}

void DelayLoops(volatile UINT32 N) {
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

void LapicWaitIcr(void) {
    while (LapicRead(LAPIC_ICR_LO) & (1u << 12)) {
        __asm__ volatile ("pause");
    }
}

void LapicSendIpi(UINT8 ApicId, UINT32 Lo) {
    LapicWaitIcr();
    LapicWrite(LAPIC_ICR_HI, ((UINT32)ApicId) << 24);
    LapicWrite(LAPIC_ICR_LO, Lo);
    LapicWaitIcr();
}

/* 超时后把跳板改成 cli;hlt，迟到 SIPI 也只会停住 */
void NeutralizeTrampoline(void) {
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

void ParkAp(UINT8 ApicId) {
    NeutralizeTrampoline();
    LapicSendIpi(ApicId, 0x0000C500u);
    DelayLoops(SMP_INIT_GAP_LOOPS);
    LapicSendIpi(ApicId, 0x00008500u);
}

void MemCopy(void *Dst, const void *Src, UINTN Len) {
    UINT8 *D = (UINT8 *)Dst;
    const UINT8 *S = (const UINT8 *)Src;
    UINTN i;
    for (i = 0; i < Len; i++) {
        D[i] = S[i];
    }
}

void MemZero(void *Dst, UINTN Len) {
    UINT8 *D = (UINT8 *)Dst;
    UINTN i;
    for (i = 0; i < Len; i++) {
        D[i] = 0;
    }
}

/* 保证 BSP 在 gApicIds[0]，便于 HalCpuGetId()==0 表示 BSP */
void SetupTrampolineGdt(void) {
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

int StartOneAp(UINT8 ApicId, UINT32 LogicalCpu) {
    SMP_BOOT_PARAM *Param = (SMP_BOOT_PARAM *)(UINTN)SMP_PARAM_PHYS;
    UINTN TrampSize =
        (UINTN)(_binary_SmpTramp_bin_end - _binary_SmpTramp_bin_start);
    UINT64 Cr3;
    int Tries;

    if (TrampSize == 0 || TrampSize > 0x1000) {
        SmpLog("Smp: Bad Trampoline Size\n");
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
        SmpLog("Smp: AP Timeout APIC=");
        SmpLogHex32(ApicId);
        SmpLog("\n");
        ParkAp(ApicId);
        return -1;
    }
    return 0;
}

