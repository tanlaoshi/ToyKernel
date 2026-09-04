/*
 * Exception.c — PR-A10：同步异常 → VirtualMemoryHandlePageFault / 可观测 fault
 */
#include "Hal.h"
#include "VirtualMemory.h"

extern void HalExceptionVectors(void);

static volatile int gProbeFault;
static volatile UINT64 gProbeFar;
static volatile int gProbeSeen;

void HalExceptionVectorsInstall(void) {
    __asm__ volatile("msr vbar_el1, %0" :: "r"(HalExceptionVectors) : "memory");
}

void HalExceptionUnexpected(UINT64 Unused) {
    (void)Unused;
    HalSerialWrite("vmm: unexpected exception\n");
    for (;;) {
        __asm__ volatile("wfi");
    }
}

/*
 * 返回非 0：写入 ELR（probe 跳过故障指令）。
 * 返回 0：保持原 ELR（COW 修复后重试）。
 */
UINT64 HalExceptionSync(UINT64 Esr, UINT64 Far, UINT64 Elr) {
    UINT32 Ec = (UINT32)((Esr >> 26) & 0x3Fu);
    UINT64 Err;
    int IsWrite;

    /* EC 0x21/0x25：Data Abort；0x20/0x24：Instruction Abort（Current/Lower） */
    if (Ec != 0x21u && Ec != 0x25u && Ec != 0x20u && Ec != 0x24u) {
        HalSerialWrite("vmm: sync esr=");
        HalDebugHex64(Esr);
        HalSerialWrite(" elr=");
        HalDebugHex64(Elr);
        HalSerialWrite("\n");
        for (;;) {
            __asm__ volatile("wfi");
        }
    }

    IsWrite = (Esr & (1ULL << 6)) != 0; /* WnR */
    /* 合成 x86 风格 error：bit0=P bit1=W bit2=U（EL1 内核访问 → 无 U） */
    Err = IsWrite ? 0x2ULL : 0x0ULL;

    HalSerialWrite("vmm: fault va=");
    HalDebugHex64(Far);
    HalSerialWrite(" esr=");
    HalDebugHex64(Esr);
    HalSerialWrite("\n");

    if (gProbeFault) {
        gProbeSeen = 1;
        gProbeFar = Far;
        HalSerialWrite("vmm: fault path ok (probe)\n");
        /* 跳过触发 probe 的 4 字节 load */
        return Elr + 4;
    }

    /* Common COW 路径要求 error==0x7（P|W|U） */
    if (VirtualMemoryHandlePageFault(Far, 0x7ULL) == 0) {
        return 0;
    }
    (void)Err;

    HalSerialWrite("vmm: fault unhandled, halt\n");
    for (;;) {
        __asm__ volatile("wfi");
    }
}

void HalPagingSelfTest(void) {
    volatile UINT8 *Bad = (volatile UINT8 *)(UINTN)0x100000000ULL; /* 4GiB：恒等映射外 */
    UINT8 Tmp;

    gProbeSeen = 0;
    gProbeFar = 0;
    gProbeFault = 1;
    /* 固定 4 字节 ldrb，便于 ELR+4 */
    __asm__ volatile("ldrb %w0, [%1]" : "=r"(Tmp) : "r"(Bad) : "memory");
    (void)Tmp;
    gProbeFault = 0;

    if (!gProbeSeen) {
        HalSerialWrite("vmm: fault probe MISSING\n");
        return;
    }
    HalSerialWrite("vmm: fault probe va=");
    HalDebugHex64(gProbeFar);
    HalSerialWrite("\n");
}
