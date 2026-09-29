/*
 * HalFrame.c — PR-S3-hal-arm-1：中断帧 / TLS / 调度进入
 */
#include "Hal.h"

static void FrameZero(HAL_INTERRUPT_FRAME *F) {
    UINTN j;
    if (!F) {
        return;
    }
    for (j = 0; j < sizeof(HAL_INTERRUPT_FRAME); j++) {
        ((UINT8 *)F)[j] = 0;
    }
}

void HalFrameSetKernelEntry(HAL_INTERRUPT_FRAME *F, UINT64 Entry, UINT64 StackTop) {
    if (!F) {
        return;
    }
    FrameZero(F);
    F->InstructionPointer = Entry;
    F->StackPointer = StackTop;
    F->Rflags = 0x3c5; /* EL1h + DAIF clear-ish；内核任务不经 eret */
}

void HalFrameSetUserEntry(HAL_INTERRUPT_FRAME *F, UINT64 Entry, UINT64 UserStackTop) {
    if (!F) {
        return;
    }
    FrameZero(F);
    F->InstructionPointer = Entry;
    F->StackPointer = UserStackTop;
    F->Rflags = 0; /* EL0t */
    F->Vec = VEC_SYSCALL;
}

void HalSetTlsBase(UINT64 UserTlsBase) {
    __asm__ volatile("msr tpidr_el0, %0" :: "r"(UserTlsBase) : "memory");
}

void HalFrameSetTls(HAL_INTERRUPT_FRAME *F, UINT64 UserTlsBase) {
    (void)F;
    (void)UserTlsBase;
}

void HalFrameCopy(HAL_INTERRUPT_FRAME *Dst, const HAL_INTERRUPT_FRAME *Src) {
    UINTN j;
    if (!Dst || !Src) {
        return;
    }
    for (j = 0; j < sizeof(HAL_INTERRUPT_FRAME); j++) {
        ((UINT8 *)Dst)[j] = ((const UINT8 *)Src)[j];
    }
}

UINT64 HalFrameGetInstructionPointer(const HAL_INTERRUPT_FRAME *F) {
    return F ? F->InstructionPointer : 0;
}

/* Linux aarch64 形：x8=号，x0..x2=参数，返回 x0 */
UINT64 HalFrameSyscallNum(const HAL_INTERRUPT_FRAME *F) {
    return F ? F->X[8] : 0;
}

UINT64 HalFrameGetArgument0(const HAL_INTERRUPT_FRAME *F) {
    return F ? F->X[0] : 0;
}

UINT64 HalFrameGetArgument1(const HAL_INTERRUPT_FRAME *F) {
    return F ? F->X[1] : 0;
}

UINT64 HalFrameGetArgument2(const HAL_INTERRUPT_FRAME *F) {
    return F ? F->X[2] : 0;
}

void HalFrameSetReturn(HAL_INTERRUPT_FRAME *F, UINT64 Value) {
    if (F) {
        F->X[0] = Value;
    }
}

void HalFrameSetReturn2(HAL_INTERRUPT_FRAME *F, UINT64 A, UINT64 B) {
    if (F) {
        F->X[0] = A;
        F->X[1] = B;
    }
}

UINT64 HalFrameGetStackPointer(const HAL_INTERRUPT_FRAME *F) {
    return F ? F->StackPointer : 0;
}

void HalFrameSetStackPointer(HAL_INTERRUPT_FRAME *F, UINT64 Sp) {
    if (F) {
        F->StackPointer = Sp;
    }
}

void HalFrameSetInstructionPointer(HAL_INTERRUPT_FRAME *F, UINT64 Ip) {
    if (F) {
        F->InstructionPointer = Ip;
    }
}

void HalFrameSetArgument0(HAL_INTERRUPT_FRAME *F, UINT64 Value) {
    if (F) {
        F->X[0] = Value;
    }
}

void HalFrameSetArgument1(HAL_INTERRUPT_FRAME *F, UINT64 Value) {
    if (F) {
        F->X[1] = Value;
    }
}

/* AArch64：x0=sig，x30=返回点，ELR=handler */
int HalFrameSignalSetup(HAL_INTERRUPT_FRAME *F, UINT64 Handler, UINT64 Sig,
                        UINT64 *OutResumeIp, UINT64 *OutPushSp) {
    if (!F || Handler < 2) {
        return -1;
    }
    (void)OutResumeIp;
    (void)OutPushSp;
    F->X[30] = F->InstructionPointer;
    F->X[0] = Sig;
    F->InstructionPointer = Handler;
    return 0;
}

UINT64 HalInterruptDispatch(struct HAL_INTERRUPT_FRAME *Frame) {
    (void)Frame;
    return 0;
}
void HalSchedulerEnter(struct HAL_INTERRUPT_FRAME *Frame) {
    UINT64 Entry;
    UINT64 Stack;

    if (!Frame || Frame->InstructionPointer == 0) {
        HalVirtPlatformIdleLoop();
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
    HalVirtPlatformIdleLoop();
}
/* HalUserEnter 在 Vectors.S */

/* 分页实现见 PageTable.c（PR-A7） */
