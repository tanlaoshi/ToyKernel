/*
 * Trap.c — PR-A10 缺页 + PR-A11 U-mode ecall / 自测
 */
#include "Hal.h"
#include "Syscall.h"
#include "VirtualMemory.h"
#include "PhysicalMemory.h"

extern void HalTrapVector(void);
extern void HalUserSelfTestReturn(void);

static volatile int gProbeFault;
static volatile UINT64 gProbeFar;
static volatile int gProbeSeen;

static volatile int gUserSelfTest;
static UINT64 gUserSelfRoot;
static VM_ADDR_SPACE *gUserSelfSpace;
static HAL_FRAME gUserSelfFrame;
static UINT8 gUserSelfKStack[8192] __attribute__((aligned(16)));

#define SSTATUS_SPIE (1ULL << 5)
#define SSTATUS_SPP  (1ULL << 8)
#define SSTATUS_SUM  (1ULL << 18)

void HalTrapVectorInstall(void) {
    __asm__ volatile("csrw sscratch, zero");
    __asm__ volatile("csrw stvec, %0" :: "r"((UINT64)(UINTN)HalTrapVector) : "memory");
}

static UINT64 HalTrapKernelSync(UINT64 Cause, UINT64 Stval, UINT64 Sepc) {
    int IsWrite;

    if (Cause & (1ULL << 63)) {
        /* 中断应在 HalTrapDispatch 先处理；此处不应到达 */
        return 0;
    }

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
    (void)IsWrite;

    HalSerialWrite("vmm: fault va=");
    HalDebugHex64(Stval);
    HalSerialWrite(" cause=");
    HalDebugHex64(Cause);
    HalSerialWrite("\n");

    if (gProbeFault) {
        gProbeSeen = 1;
        gProbeFar = Stval;
        HalSerialWrite("vmm: fault path ok (probe)\n");
        return Sepc + 4;
    }

    if (VirtualMemoryHandlePageFault(Stval, 0x7ULL) == 0) {
        return 0;
    }

    HalSerialWrite("vmm: fault unhandled, halt\n");
    for (;;) {
        __asm__ volatile("wfi");
    }
}

/*
 * 统一陷阱分发：内核缺页 / 用户 ecall / 用户缺页。
 * 返回值同 x86：0=恢复本帧；非 0=调度标签|新帧。
 */
UINT64 HalTrapDispatch(HAL_FRAME *Frame) {
    UINT64 Cause;
    UINT64 Stval;
    UINT64 Status;
    UINT64 Ret;
    int FromUser;

    if (!Frame) {
        return 0;
    }

    __asm__ volatile("csrr %0, scause" : "=r"(Cause));
    __asm__ volatile("csrr %0, stval" : "=r"(Stval));
    Status = Frame->Rflags;
    FromUser = (Status & SSTATUS_SPP) == 0;

    /* PR-A13：supervisor timer interrupt（cause 5） */
    if (Cause & (1ULL << 63)) {
        UINT64 Code = Cause & 0xFFULL;
        if (Code == 5) {
            extern void HalTimerIrq(void);
            HalTimerIrq();
            return 0;
        }
        HalSerialWrite("vmm: irq cause=");
        HalDebugHex64(Cause);
        HalSerialWrite("\n");
        return 0;
    }

    if (!FromUser) {
        UINT64 NewSepc = HalTrapKernelSync(Cause, Stval, Frame->InstructionPointer);
        if (NewSepc != 0) {
            Frame->InstructionPointer = NewSepc;
        }
        return 0;
    }

    /* 用户：ecall = 8（U-mode）或 9（S，不应出现） */
    if (Cause == 8) {
        Frame->Vec = VEC_SYSCALL;
        Frame->InstructionPointer += 4; /* ecall 下一条 */
        if (gUserSelfTest && HalFrameSyscallNum(Frame) == SYS_EXIT) {
            HalSerialWrite("user: U-mode syscall ok (exit)\n");
            gUserSelfTest = 0;
            if (gUserSelfRoot) {
                HalLoadPageTable(VirtualMemoryKernelRoot());
            }
            __asm__ volatile("csrw sscratch, zero");
            HalUserSelfTestReturn();
            return 0;
        }
        Ret = SyscallDispatch(Frame);
        return Ret;
    }

    if (Cause == 12 || Cause == 13 || Cause == 15) {
        Frame->Vec = 0;
        Frame->Err = 0x7;
        HalSerialWrite("vmm: user fault va=");
        HalDebugHex64(Stval);
        HalSerialWrite("\n");
        if (VirtualMemoryHandlePageFault(Stval, 0x7ULL) == 0) {
            return 0;
        }
        HalSerialWrite("vmm: user fault unhandled, halt\n");
        for (;;) {
            __asm__ volatile("wfi");
        }
    }

    HalSerialWrite("vmm: user trap cause=");
    HalDebugHex64(Cause);
    HalSerialWrite("\n");
    for (;;) {
        __asm__ volatile("wfi");
    }
}

void HalPagingSelfTest(void) {
    volatile UINT8 *Bad = (volatile UINT8 *)(UINTN)0x180000000ULL;
    UINT8 Tmp;

    gProbeSeen = 0;
    gProbeFar = 0;
    gProbeFault = 1;
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

static void BuildUserStub(UINT8 *Page, UINT64 CodeVa) {
    /*
     * 固定 VA 小程序（rv64i，无压缩）：
     *   li a0,1; li a1,msg; li a2,12; li a7,1; ecall
     *   li a0,0; li a7,0; ecall
     * msg @ +0x80: "Hello U-mode!\n"
     */
    UINT32 *I = (UINT32 *)(void *)Page;
    UINT64 MsgVa = CodeVa + 0x80;
    const char *Msg = "Hello U-mode!\n";
    UINTN n;
    UINT32 Hi;
    UINT32 Lo;

    /* auipc a1, 0; addi a1, a1, (MsgVa - CodeVa) — 用 lui/addi 绝对地址更稳 */
    Hi = (UINT32)((MsgVa + 0x800) >> 12);
    Lo = (UINT32)(MsgVa - ((UINT64)Hi << 12));

    I[0] = 0x00100513u;                         /* addi a0,x0,1 */
    I[1] = (Hi << 12) | (11 << 7) | 0x37u;      /* lui a1, hi */
    I[2] = (Lo << 20) | (11 << 15) | (11 << 7) | 0x13u; /* addi a1,a1,lo */
    I[3] = 0x00e00613u;                         /* addi a2,x0,14 */
    I[4] = 0x00100893u;                         /* addi a7,x0,1 */
    I[5] = 0x00000073u;                         /* ecall */
    I[6] = 0x00000513u;                         /* addi a0,x0,0 */
    I[7] = 0x00000893u;                         /* addi a7,x0,0 */
    I[8] = 0x00000073u;                         /* ecall */

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
    UINT64 Status;
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
    __asm__ volatile("fence.i" ::: "memory");
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
    gUserSelfFrame.InstructionPointer = CodeVa;
    gUserSelfFrame.StackPointer = StackTop;
    __asm__ volatile("csrr %0, sstatus" : "=r"(Status));
    Status |= SSTATUS_SUM | SSTATUS_SPIE;
    Status &= ~SSTATUS_SPP;
    gUserSelfFrame.Rflags = Status;

    gUserSelfSpace = Space;
    gUserSelfRoot = VirtualMemorySpaceRoot(Space);
    gUserSelfTest = 1;
    HalLoadPageTable(gUserSelfRoot);

    Ksp = (UINT64)(UINTN)(gUserSelfKStack + sizeof(gUserSelfKStack));
    HalSerialWrite("user: enter U-mode at ");
    HalDebugHex64(CodeVa);
    HalSerialWrite("\n");
    HalUserSelfTestEnter(Ksp, &gUserSelfFrame);

    HalSerialWrite("user: back in S-mode (selftest ok)\n");
    if (gUserSelfSpace) {
        VirtualMemorySpaceDestroy(gUserSelfSpace);
        gUserSelfSpace = 0;
    }
    gUserSelfRoot = 0;
    HalLoadPageTable(VirtualMemoryKernelRoot());
    __asm__ volatile("csrw sscratch, zero");
}
