/*
 * HAL/arm64/Hal.c — ARM64 HAL（PR-A7 可链接；PR-A13 GIC + CNTV IRQ）
 */
#include "Hal.h"

static UINT64 gCntLast;
static UINT64 gCntFreq;
static UINT64 gCntPeriod;
static UINT32 gTimerMs = 10;
static int gTimerReady;
static int gTimerIrq;

extern void HalExceptionVectorsInstall(void);
extern void HalUserEnter(struct HAL_FRAME *Frame);
extern void HalGicInit(void);
extern UINT32 HalGicAck(void);
extern void HalGicEoi(UINT32 IntId);
extern int HalGicIsTimer(UINT32 IntId);

int HalInit(void) {
    /* PR-A14：BSP 逻辑 CPU=0（TPIDR_EL1） */
    __asm__ volatile("msr tpidr_el1, xzr" ::: "memory");
    return 0;
}

void HalCpuHalt(void) {
    /* PR-A13：开 IRQ + WFI，由 CNTV/GIC 唤醒（对齐 x86 sti;hlt;cli） */
    if (gTimerIrq) {
        HalIrqEnable();
        __asm__ volatile("wfi" ::: "memory");
        HalIrqDisable();
        return;
    }
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

void HalIrqEnable(void) {
    __asm__ volatile("msr daifclr, #2" ::: "memory");
}
void HalIrqDisable(void) {
    __asm__ volatile("msr daifset, #2" ::: "memory");
}
UINT64 HalIrqSave(void) {
    UINT64 Flags;
    __asm__ volatile("mrs %0, daif" : "=r"(Flags));
    HalIrqDisable();
    return Flags;
}
void HalIrqRestore(UINT64 Flags) {
    __asm__ volatile("msr daif, %0" ::"r"(Flags) : "memory");
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
void HalIrqEoi(UINT32 Vector) {
    HalGicEoi(Vector);
}

static void ArmTimerArm(void) {
    __asm__ volatile("msr cntv_tval_el0, %0" ::"r"(gCntPeriod) : "memory");
    __asm__ volatile("msr cntv_ctl_el0, %0" ::"r"(1ULL) : "memory");
    __asm__ volatile("isb" ::: "memory");
}

void HalTimerInit(void) {
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(gCntFreq));
    if (gCntFreq == 0) {
        gCntFreq = 62500000ULL; /* QEMU virt 常见缺省 */
    }
    gCntPeriod = gCntFreq / 100; /* ~10ms */
    if (gCntPeriod == 0) {
        gCntPeriod = 1;
    }
    __asm__ volatile("mrs %0, cntvct_el0" : "=r"(gCntLast));
    gTimerReady = 1;
}

void HalTimerSetInterval(UINT32 Milliseconds) {
    if (Milliseconds == 0) {
        Milliseconds = 10;
    }
    gTimerMs = Milliseconds;
    if (gCntFreq == 0) {
        return;
    }
    gCntPeriod = (gCntFreq * (UINT64)Milliseconds) / 1000ULL;
    if (gCntPeriod == 0) {
        gCntPeriod = 1;
    }
}

void HalTimerAck(void) {
    ArmTimerArm();
}

void HalTimerStart(void) {
    if (!gTimerReady) {
        HalTimerInit();
    }
    HalExceptionVectorsInstall();
    HalGicInit();
    HalTimerSetInterval(gTimerMs);
    ArmTimerArm();
    gTimerIrq = 1;
    HalSerialWrite("timer: Arm64 CNTV+GIC irq\n");
}

/* PR-A14：AP 在 BSP HalTimerStart 之前也可本地开 CNTV（gCntPeriod 已在 InitCpu） */
void HalTimerStartAp(void) {
    extern void HalGicInitCpu(void);

    if (!gTimerReady) {
        HalTimerInit();
    }
    HalExceptionVectorsInstall();
    HalGicInitCpu();
    ArmTimerArm();
    gTimerIrq = 1;
}

void HalTimerPoll(void) {
    UINT64 Now;
    UINT64 Delta;

    if (!gTimerReady || gTimerIrq) {
        return;
    }
    __asm__ volatile("mrs %0, cntvct_el0" : "=r"(Now));
    Delta = Now - gCntLast;
    while (Delta >= gCntPeriod) {
        HalCpuTickInc();
        gCntLast += gCntPeriod;
        Delta -= gCntPeriod;
    }
}

/* PR-A13：Current/Lower EL IRQ 入口 */
void HalExceptionIrq(void) {
    UINT32 Id = HalGicAck();
    if (HalGicIsTimer(Id)) {
        HalCpuTickInc();
        HalTimerAck();
    }
    HalGicEoi(Id);
}

int HalPlatformVirtConsole(void) {
    return 1;
}

void HalVirtIdleLoop(void) {
    HalSerialWrite("virt: idle loop (no console)\n");
    for (;;) {
        HalCpuHalt();
    }
}

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
    F->InstructionPointer = Entry;
    F->StackPointer = StackTop;
    F->Rflags = 0x3c5; /* EL1h + DAIF clear-ish；内核任务不经 eret */
}

void HalFrameSetUserEntry(HAL_FRAME *F, UINT64 Entry, UINT64 UserStackTop) {
    if (!F) {
        return;
    }
    FrameZero(F);
    F->InstructionPointer = Entry;
    F->StackPointer = UserStackTop;
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

UINT64 HalFrameGetInstructionPointer(const HAL_FRAME *F) {
    return F ? F->InstructionPointer : 0;
}

/* Linux aarch64 形：x8=号，x0..x2=参数，返回 x0 */
UINT64 HalFrameSyscallNum(const HAL_FRAME *F) {
    return F ? F->X[8] : 0;
}

UINT64 HalFrameGetArgument0(const HAL_FRAME *F) {
    return F ? F->X[0] : 0;
}

UINT64 HalFrameGetArgument1(const HAL_FRAME *F) {
    return F ? F->X[1] : 0;
}

UINT64 HalFrameGetArgument2(const HAL_FRAME *F) {
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

    if (!Frame || Frame->InstructionPointer == 0) {
        HalVirtIdleLoop();
        return;
    }
    Entry = Frame->InstructionPointer;
    Stack = Frame->StackPointer;
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
const char *HalCpuInfo(void) { return "ARM64 (virt A14)"; }

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

/* HalCpuCount / Id / ticks / HalSmpStartAps → Smp.c（PR-A14） */
