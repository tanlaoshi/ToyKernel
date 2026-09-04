/*
 * Exception.c — PR-A10 缺页 + PR-A11 用户 SVC / 自测
 */
#include "Hal.h"
#include "Syscall.h"
#include "VirtualMemory.h"
#include "PhysicalMemory.h"

extern void HalExceptionVectors(void);
extern void HalUserSelfTestReturn(void);

static volatile int gProbeFault;
static volatile UINT64 gProbeFar;
static volatile int gProbeSeen;

static volatile int gUserSelfTest;
static UINT64 gUserSelfRoot;
static VM_ADDR_SPACE *gUserSelfSpace;
static HAL_FRAME gUserSelfFrame;
static UINT8 gUserSelfKStack[8192] __attribute__((aligned(16)));

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

    IsWrite = (Esr & (1ULL << 6)) != 0;
    (void)IsWrite;

    HalSerialWrite("vmm: fault va=");
    HalDebugHex64(Far);
    HalSerialWrite(" esr=");
    HalDebugHex64(Esr);
    HalSerialWrite("\n");

    if (gProbeFault) {
        gProbeSeen = 1;
        gProbeFar = Far;
        HalSerialWrite("vmm: fault path ok (probe)\n");
        return Elr + 4;
    }

    if (VirtualMemoryHandlePageFault(Far, 0x7ULL) == 0) {
        return 0;
    }

    HalSerialWrite("vmm: fault unhandled, halt\n");
    for (;;) {
        __asm__ volatile("wfi");
    }
}

/*
 * Lower EL：SVC → SyscallDispatch；缺页 → VMM；自测 exit → 回内核。
 */
UINT64 HalExceptionLower(HAL_FRAME *Frame) {
    UINT64 Esr;
    UINT32 Ec;
    UINT64 Far;
    UINT64 Ret;

    if (!Frame) {
        return 0;
    }

    __asm__ volatile("mrs %0, esr_el1" : "=r"(Esr));
    Ec = (UINT32)((Esr >> 26) & 0x3Fu);

    /* SVC64 */
    if (Ec == 0x15u) {
        Frame->Vec = VEC_SYSCALL;
        Frame->Err = 0;
        if (gUserSelfTest && HalFrameSyscallNum(Frame) == SYS_EXIT) {
            HalSerialWrite("user: EL0 syscall ok (exit)\n");
            gUserSelfTest = 0;
            if (gUserSelfRoot) {
                HalLoadPageTable(VirtualMemoryKernelRoot());
            }
            HalUserSelfTestReturn();
            return 0;
        }
        Ret = SyscallDispatch(Frame);
        return Ret;
    }

    /* Data / Instr abort from EL0 */
    if (Ec == 0x20u || Ec == 0x21u || Ec == 0x24u || Ec == 0x25u) {
        __asm__ volatile("mrs %0, far_el1" : "=r"(Far));
        Frame->Vec = 0;
        Frame->Err = 0x7;
        HalSerialWrite("vmm: user fault va=");
        HalDebugHex64(Far);
        HalSerialWrite("\n");
        if (VirtualMemoryHandlePageFault(Far, 0x7ULL) == 0) {
            return 0;
        }
        HalSerialWrite("vmm: user fault unhandled, halt\n");
        for (;;) {
            __asm__ volatile("wfi");
        }
    }

    HalSerialWrite("vmm: lower esr=");
    HalDebugHex64(Esr);
    HalSerialWrite("\n");
    for (;;) {
        __asm__ volatile("wfi");
    }
}

void HalPagingSelfTest(void) {
    /* 与用户基址错开：用户 @4GiB，probe @6GiB */
    volatile UINT8 *Bad = (volatile UINT8 *)(UINTN)0x180000000ULL;
    UINT8 Tmp;

    gProbeSeen = 0;
    gProbeFar = 0;
    gProbeFault = 1;
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

/*
 * 内嵌极小用户程序（固定 VA = HalUserCodeVirt）：
 *   write(1, "Hello EL0!\n", 11); exit(0);
 * ABI：x8=号，x0/x1/x2=参数（Linux aarch64 形）。
 */
static void BuildUserStub(UINT8 *Page, UINT64 CodeVa) {
    UINT32 *I = (UINT32 *)(void *)Page;
    UINT64 MsgVa = CodeVa + 0x80;
    const char *Msg = "Hello EL0!\n";
    UINTN n;

    /*
     * +0  mov x0,#1
     * +4  mov x2,#11
     * +8  ldr x1, +32   (literal @ +32)
     * +12 mov x8,#1
     * +16 svc #0
     * +20 mov x0,#0
     * +24 mov x8,#0
     * +28 svc #0
     * +32 .quad MsgVa
     */
    I[0] = 0xD2800020u;
    I[1] = 0xD2800162u;
    /* imm19 = (32-8)/4 = 6 */
    I[2] = 0x58000000u | (6u << 5) | 1u;
    I[3] = 0xD2800028u;
    I[4] = 0xD4000001u;
    I[5] = 0xD2800000u;
    I[6] = 0xD2800008u;
    I[7] = 0xD4000001u;
    *(UINT64 *)(void *)(Page + 32) = MsgVa;

    for (n = 0; Msg[n]; n++) {
        Page[0x80 + n] = (UINT8)Msg[n];
    }
}

extern void HalUserSelfTestEnter(UINT64 Ksp, HAL_FRAME *Frame);

void HalUserSelfTest(void) {
    VM_ADDR_SPACE *Space;
    void *CodePage;
    void *StackPage;
    UINT64 CodeVa;
    UINT64 StackVa;
    UINT64 StackTop;
    UINT64 Spsr;
    UINT64 Ksp;
    UINTN j;

    CodeVa = HalUserCodeVirt();
    StackVa = HalUserStackVirt();
    StackTop = StackVa + HalUserStackSize();

    Space = VirtualMemorySpaceCreate();
    if (!Space) {
        HalSerialWrite("user: selftest no space\n");
        return;
    }
    CodePage = VirtualMemorySpaceAllocateAndTrack(Space);
    StackPage = VirtualMemorySpaceAllocateAndTrack(Space);
    if (!CodePage || !StackPage) {
        HalSerialWrite("user: selftest no pages\n");
        VirtualMemorySpaceDestroy(Space);
        return;
    }
    BuildUserStub((UINT8 *)CodePage, CodeVa);
    __asm__ volatile(
        "dc cvau, %0\n"
        "dsb ish\n"
        "ic ivau, %0\n"
        "dsb ish\n"
        "isb\n"
        :: "r"(CodePage) : "memory");
    if (VirtualMemorySpaceMapPage(Space, CodeVa, (UINT64)(UINTN)CodePage,
                                  HAL_PAGE_PRESENT | HAL_PAGE_USER) != 0 ||
        VirtualMemorySpaceMapPage(Space, StackVa, (UINT64)(UINTN)StackPage,
                                  HAL_PAGE_PRESENT | HAL_PAGE_WRITABLE | HAL_PAGE_USER) != 0) {
        HalSerialWrite("user: selftest map failed\n");
        VirtualMemorySpaceDestroy(Space);
        return;
    }

    for (j = 0; j < sizeof(gUserSelfFrame); j++) {
        ((UINT8 *)&gUserSelfFrame)[j] = 0;
    }
    gUserSelfFrame.Rip = CodeVa;
    gUserSelfFrame.Rsp = StackTop;
    Spsr = 0;
    gUserSelfFrame.Rflags = Spsr;

    gUserSelfSpace = Space;
    gUserSelfRoot = VirtualMemorySpaceRoot(Space);
    gUserSelfTest = 1;
    HalLoadPageTable(gUserSelfRoot);

    Ksp = (UINT64)(UINTN)(gUserSelfKStack + sizeof(gUserSelfKStack));
    HalSerialWrite("user: enter EL0 at ");
    HalDebugHex64(CodeVa);
    HalSerialWrite("\n");
    HalUserSelfTestEnter(Ksp, &gUserSelfFrame);

    HalSerialWrite("user: back in EL1 (selftest ok)\n");
    if (gUserSelfSpace) {
        VirtualMemorySpaceDestroy(gUserSelfSpace);
        gUserSelfSpace = 0;
    }
    gUserSelfRoot = 0;
    HalLoadPageTable(VirtualMemoryKernelRoot());
}
