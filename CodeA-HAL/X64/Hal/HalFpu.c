/*
 * HalFpu.c — PR-UI-ttf-fpu：本核开 OSFXSR + Begin/End 岛（x86）
 *
 * 其余内核 TU 仍 -mgeneral-regs-only。本文件不用 XMM。
 */
#include "Hal.h"

#define CR0_EM       (1ULL << 2)
#define CR0_MP       (1ULL << 1)
#define CR0_TS       (1ULL << 3)
#define CR4_OSFXSR   (1ULL << 9)
#define CR4_OSXMMEXCPT (1ULL << 10)
#define CPUID_FXSR   (1u << 24)
#define CPUID_SSE    (1u << 25)
#define FX_BYTES     512u

static int gCapable;
static int gOk;
static UINT8 gDepth[HAL_MAX_CPUS];
static UINT8 gFx[HAL_MAX_CPUS][FX_BYTES] __attribute__((aligned(16)));

int HalFpuSseProbe(void);

static UINT32 CpuIdEdx1(void) {
    UINT32 A;
    UINT32 B;
    UINT32 C;
    UINT32 D;

    __asm__ volatile ("cpuid" : "=a"(A), "=b"(B), "=c"(C), "=d"(D) : "a"(1u), "c"(0u));
    (void)A;
    (void)B;
    (void)C;
    return D;
}

static UINT32 CpuIndex(void) {
    UINT32 Id = HalCpuGetId();

    if (Id >= HAL_MAX_CPUS) {
        return 0;
    }
    return Id;
}

void HalFpuEnableThisCpu(void) {
    UINT64 Cr0;
    UINT64 Cr4;
    UINT32 Edx;

    Edx = CpuIdEdx1();
    if ((Edx & CPUID_FXSR) == 0 || (Edx & CPUID_SSE) == 0) {
        gCapable = 0;
        return;
    }
    gCapable = 1;

    __asm__ volatile ("mov %%cr0, %0" : "=r"(Cr0));
    Cr0 &= ~CR0_EM;
    Cr0 &= ~CR0_TS;
    Cr0 |= CR0_MP;
    __asm__ volatile ("mov %0, %%cr0" :: "r"(Cr0) : "memory");

    __asm__ volatile ("mov %%cr4, %0" : "=r"(Cr4));
    Cr4 |= CR4_OSFXSR;
    Cr4 |= CR4_OSXMMEXCPT;
    __asm__ volatile ("mov %0, %%cr4" :: "r"(Cr4) : "memory");

    __asm__ volatile ("fninit" ::: "memory");
}

int HalFpuBegin(void) {
    UINT32 Cpu;
    UINT64 Flags;

    if (!gCapable) {
        return 0;
    }
    Cpu = CpuIndex();
    Flags = HalIrqSave();
    if (gDepth[Cpu] == 0) {
        __asm__ volatile ("fxsave %0" : "=m"(gFx[Cpu]) : : "memory");
    }
    if (gDepth[Cpu] < 255u) {
        gDepth[Cpu]++;
    }
    HalIrqRestore(Flags);
    return 1;
}

void HalFpuEnd(void) {
    UINT32 Cpu;
    UINT64 Flags;

    if (!gCapable) {
        return;
    }
    Cpu = CpuIndex();
    Flags = HalIrqSave();
    if (gDepth[Cpu] > 0) {
        gDepth[Cpu]--;
        if (gDepth[Cpu] == 0) {
            __asm__ volatile ("fxrstor %0" :: "m"(gFx[Cpu]) : "memory");
        }
    }
    HalIrqRestore(Flags);
}

int HalFpuOk(void) {
    return gOk;
}

int HalFpuSelfTest(void) {
    int Got;

    if (!gCapable) {
        HalFpuEnableThisCpu();
    }
    if (!gCapable) {
        gOk = 0;
        return -1;
    }
    if (!HalFpuBegin()) {
        gOk = 0;
        return -1;
    }
    Got = HalFpuSseProbe();
    HalFpuEnd();
    if (Got != 30) {
        gOk = 0;
        return -1;
    }
    gOk = 1;
    return 0;
}
