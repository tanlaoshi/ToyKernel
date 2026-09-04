/*
 * HAL/arm64/Hal.c — ARM64 HAL（PR-A7 可链接；PR-A8 virt 子集 + 轮询时钟）
 */
#include "Hal.h"

static volatile UINT64 gVirtTicks;
static UINT64 gCntLast;
static UINT64 gCntFreq;
static int gTimerReady;

int HalInit(void) {
    return 0;
}

void HalCpuHalt(void) {
    /* virt：无定时 IRQ，不能 WFI 永睡；供 Shell/Gui 任务轮询返回 */
    HalTimerPoll();
    HalCpuRelax();
}
void HalCpuPark(void) {
    for (;;) {
        __asm__ volatile("wfi");
    }
}
void HalCpuReboot(void) { HalCpuPark(); }
void HalCpuShutdown(void) { HalCpuPark(); }

void HalIrqEnable(void) { }
void HalIrqDisable(void) { }
UINT64 HalIrqSave(void) {
    return 0;
}
void HalIrqRestore(UINT64 Flags) {
    (void)Flags;
}
void HalCpuRelax(void) {
    __asm__ volatile("yield" ::: "memory");
}
void HalIrqVectorSet(UINT32 Vector, void *Handler, UINT8 Type) {
    (void)Vector; (void)Handler; (void)Type;
}
void HalIrqRegister(UINT32 Vector, void (*Handler)(void)) {
    (void)Vector; (void)Handler;
}
void HalIrqUnregister(UINT32 Vector) { (void)Vector; }
void HalIrqEoi(UINT32 Vector) { (void)Vector; }

void HalTimerInit(void) {
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(gCntFreq));
    if (gCntFreq == 0) {
        gCntFreq = 62500000ULL; /* QEMU virt 常见缺省 */
    }
    __asm__ volatile("mrs %0, cntvct_el0" : "=r"(gCntLast));
    gTimerReady = 1;
}

void HalTimerSetInterval(UINT32 Milliseconds) { (void)Milliseconds; }
void HalTimerAck(void) { }
void HalTimerStart(void) {
    /* A8：无 GIC 定时 IRQ，由 HalVirtIdleLoop 调 HalTimerPoll */
}

void HalTimerPoll(void) {
    UINT64 Now;
    UINT64 Period;
    UINT64 Delta;

    if (!gTimerReady) {
        return;
    }
    __asm__ volatile("mrs %0, cntvct_el0" : "=r"(Now));
    Period = gCntFreq / 100; /* ~10ms */
    if (Period == 0) {
        Period = 1;
    }
    Delta = Now - gCntLast;
    while (Delta >= Period) {
        HalCpuTickInc();
        gCntLast += Period;
        Delta -= Period;
    }
}

int HalPlatformVirtConsole(void) {
    return 1;
}

void HalVirtIdleLoop(void) {
    /* A9：正式路径走 ConsoleSerialRun；此处仅作误入 sched 的兜底 */
    HalSerialWrite("virt: idle loop (no console)\n");
    for (;;) {
        HalTimerPoll();
        HalCpuRelax();
    }
}

extern void HalExceptionVectorsInstall(void);
extern void HalUserEnter(struct HAL_FRAME *Frame);

void HalUserInstall(void) {
    /* EL0 入口前确保向量表；SP_EL1 由当前内核栈承担 */
    HalExceptionVectorsInstall();
}

void HalSyscallInit(void) {
    HalExceptionVectorsInstall();
    HalSerialWrite("syscall: Arm64 SVC (EL0) ready\n");
    HalUserSelfTest();
}

void HalSetKernelStack(UINT64 StackTop) {
    /* EL1 用 SP_EL1：切栈留给调用方；此处记录供将来 IRQ */
    (void)StackTop;
}

/* 躲开内核 @0x40000000 恒等映射：用户区从 4GiB 起 */
UINT64 HalUserCodeVirt(void) {
    return 0x100000000ULL;
}
UINT64 HalUserStackVirt(void) {
    return 0x100100000ULL;
}
UINT64 HalUserStackSize(void) {
    return 0x4000ULL;
}
UINT64 HalUserBrkMax(void) {
    return 0x100080000ULL;
}
UINT64 HalUserSoBase(void) {
    return 0x100080000ULL;
}
UINT64 HalUserVirtEnd(void) {
    return HalUserStackVirt() + HalUserStackSize();
}

static void FrameZero(HAL_FRAME *F) {
    UINTN j;
    if (!F) {
        return;
    }
    for (j = 0; j < sizeof(HAL_FRAME); j++) {
        ((UINT8 *)F)[j] = 0;
    }
}

void HalFrameSetKernelEntry(HAL_FRAME *F, UINT64 Entry, UINT64 StackTop) {
    if (!F) {
        return;
    }
    FrameZero(F);
    F->Rip = Entry;
    F->Rsp = StackTop;
    F->Rflags = 0x3c5; /* EL1h + DAIF clear-ish；内核任务不经 eret */
}

void HalFrameSetUserEntry(HAL_FRAME *F, UINT64 Entry, UINT64 UserRsp) {
    if (!F) {
        return;
    }
    FrameZero(F);
    F->Rip = Entry;
    F->Rsp = UserRsp;
    F->Rflags = 0; /* EL0t */
    F->Vec = VEC_SYSCALL;
}

void HalFrameCopy(HAL_FRAME *Dst, const HAL_FRAME *Src) {
    UINTN j;
    if (!Dst || !Src) {
        return;
    }
    for (j = 0; j < sizeof(HAL_FRAME); j++) {
        ((UINT8 *)Dst)[j] = ((const UINT8 *)Src)[j];
    }
}

UINT64 HalFrameGetRip(const HAL_FRAME *F) {
    return F ? F->Rip : 0;
}

/* Linux aarch64 形：x8=号，x0..x2=参数，返回 x0 */
UINT64 HalFrameSyscallNum(const HAL_FRAME *F) {
    return F ? F->X[8] : 0;
}

UINT64 HalFrameArg0(const HAL_FRAME *F) {
    return F ? F->X[0] : 0;
}

UINT64 HalFrameArg1(const HAL_FRAME *F) {
    return F ? F->X[1] : 0;
}

UINT64 HalFrameArg2(const HAL_FRAME *F) {
    return F ? F->X[2] : 0;
}

void HalFrameSetReturn(HAL_FRAME *F, UINT64 Value) {
    if (F) {
        F->X[0] = Value;
    }
}

void HalFrameSetReturn2(HAL_FRAME *F, UINT64 A, UINT64 B) {
    if (F) {
        F->X[0] = A;
        F->X[1] = B;
    }
}

UINT64 HalInterruptDispatch(struct HAL_FRAME *Frame) {
    (void)Frame;
    return 0;
}
void HalSchedulerEnter(struct HAL_FRAME *Frame) {
    UINT64 Entry;
    UINT64 Stack;

    if (!Frame || Frame->Rip == 0) {
        HalVirtIdleLoop();
        return;
    }
    Entry = Frame->Rip;
    Stack = Frame->Rsp;
    __asm__ volatile(
        "mov sp, %0\n"
        "br  %1\n"
        :
        : "r"(Stack), "r"(Entry)
        : "memory");
    HalVirtIdleLoop();
}
/* HalUserEnter 在 Vectors.S */

/* 分页实现见 Page.c（PR-A7） */

const char *HalArchName(void) { return "aarch64"; }
const char *HalCpuInfo(void) { return "ARM64 (virt A11)"; }

UINT16 HalElfMachine(void) {
    return 183; /* EM_AARCH64 */
}

/* PR-A12：AArch64 ELF reloc → Common Elf.c 可处理 kind */
HAL_ELF_RELOC_KIND HalElfRelocKind(UINT32 Type) {
    switch (Type) {
    case 1027: /* R_AARCH64_RELATIVE */
        return HAL_ELF_RELOC_RELATIVE;
    case 257:  /* R_AARCH64_ABS64 */
        return HAL_ELF_RELOC_ABS64;
    case 1025: /* R_AARCH64_GLOB_DAT */
        return HAL_ELF_RELOC_GLOB_DAT;
    case 1026: /* R_AARCH64_JUMP_SLOT */
        return HAL_ELF_RELOC_JUMP_SLOT;
    case 1024: /* R_AARCH64_COPY */
        return HAL_ELF_RELOC_COPY;
    default:
        return HAL_ELF_RELOC_UNSUPPORTED;
    }
}

void HalSyncICache(void *Addr, UINTN Size) {
    UINT8 *P = (UINT8 *)Addr;
    UINT8 *End;
    UINT64 Line = 64;

    if (!Addr || Size == 0) {
        return;
    }
    End = P + Size;
    for (; P < End; P += Line) {
        __asm__ volatile("dc cvau, %0" ::"r"(P) : "memory");
    }
    __asm__ volatile("dsb ish" ::: "memory");
    for (P = (UINT8 *)Addr; P < End; P += Line) {
        __asm__ volatile("ic ivau, %0" ::"r"(P) : "memory");
    }
    __asm__ volatile("dsb ish\n isb" ::: "memory");
}

void HalDebugWrite(const char *Text) {
    HalSerialWrite(Text);
}
void HalDebugHex32(UINT32 Value) {
    char Buf[9];
    HalSerialHexFormat(Buf, Value, 8);
    HalSerialWrite(Buf);
}
void HalDebugHex64(UINT64 Value) {
    char Buf[17];
    HalSerialHexFormat(Buf, Value, 16);
    HalSerialWrite(Buf);
}

int HalCpuCount(void) { return 1; }
UINT32 HalCpuId(void) { return 0; }
int HalCpuIsBsp(void) { return 1; }
UINT64 HalCpuTicks(UINT32 Cpu) {
    (void)Cpu;
    return gVirtTicks;
}
void HalCpuTickInc(void) {
    gVirtTicks++;
}
int HalSmpStartAps(void) { return 0; }
