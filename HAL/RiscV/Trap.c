/*
 * Trap.c — PR-A10：S-mode 缺页 → Common / 可观测 fault
 */
#include "Hal.h"
#include "VirtualMemory.h"

extern void HalTrapVector(void);

static volatile int gProbeFault;
static volatile UINT64 gProbeFar;
static volatile int gProbeSeen;

void HalTrapVectorInstall(void) {
    __asm__ volatile("csrw stvec, %0" :: "r"((UINT64)(UINTN)HalTrapVector) : "memory");
}

/*
 * 返回非 0：新 sepc（probe 跳过）；0：保持 sepc（COW 重试）。
 */
UINT64 HalTrapSync(UINT64 Cause, UINT64 Stval, UINT64 Sepc) {
    UINT64 Err;
    int IsWrite;

    /* 中断（最高位）不归本路径处理 */
    if (Cause & (1ULL << 63)) {
        HalSerialWrite("vmm: unexpected interrupt cause=");
        HalDebugHex64(Cause);
        HalSerialWrite("\n");
        for (;;) {
            __asm__ volatile("wfi");
        }
    }

    /* 12=I-fault, 13=load, 15=store */
    if (Cause != 12 && Cause != 13 && Cause != 15) {
        HalSerialWrite("vmm: trap cause=");
        HalDebugHex64(Cause);
        HalSerialWrite(" epc=");
        HalDebugHex64(Sepc);
        HalSerialWrite("\n");
        for (;;) {
            __asm__ volatile("wfi");
        }
    }

    IsWrite = (Cause == 15);
    Err = IsWrite ? 0x2ULL : 0x0ULL;

    HalSerialWrite("vmm: fault va=");
    HalDebugHex64(Stval);
    HalSerialWrite(" cause=");
    HalDebugHex64(Cause);
    HalSerialWrite("\n");

    if (gProbeFault) {
        gProbeSeen = 1;
        gProbeFar = Stval;
        HalSerialWrite("vmm: fault path ok (probe)\n");
        /* 压缩指令可能 2 字节；内核用 -march=rv64imac，load 常为 4B c.ld/ld。
         * 用 4：与 aarch64 probe 一致；若偶发再对齐可改为读指令长度。 */
        return Sepc + 4;
    }

    if (VirtualMemoryHandlePageFault(Stval, 0x7ULL) == 0) {
        return 0;
    }
    (void)Err;

    HalSerialWrite("vmm: fault unhandled, halt\n");
    for (;;) {
        __asm__ volatile("wfi");
    }
}

void HalPagingSelfTest(void) {
    volatile UINT8 *Bad = (volatile UINT8 *)(UINTN)0x100000000ULL;
    UINT8 Tmp;

    gProbeSeen = 0;
    gProbeFar = 0;
    gProbeFault = 1;
    /* 禁止压缩，保证 sepc+4 */
    __asm__ volatile(
        ".option push\n"
        ".option norvc\n"
        "lbu %0, 0(%1)\n"
        ".option pop\n"
        : "=r"(Tmp)
        : "r"(Bad)
        : "memory");
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
