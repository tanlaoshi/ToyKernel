/*
 * HalFrame.c — PR-S3-hal-riscv-1：中断帧 / TLS / 调度进入
 */
#include "Hal.h"

#define SSTATUS_SPIE (1ULL << 5)
#define SSTATUS_SPP  (1ULL << 8)

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
    /* PR-V-input-fix：Create 帧若经 sret 恢复须保持 S-mode（SPP=1） */
    F->Rflags = SSTATUS_SPP | SSTATUS_SPIE;
}

void HalFrameSetUserEntry(HAL_INTERRUPT_FRAME *F, UINT64 Entry, UINT64 UserStackTop) {
    UINT64 Status;

    if (!F) {
        return;
    }
    FrameZero(F);
    F->InstructionPointer = Entry;
    F->StackPointer = UserStackTop;
    __asm__ volatile("csrr %0, sstatus" : "=r"(Status));
    Status |= (1ULL << 18) | (1ULL << 5); /* SUM|SPIE */
    Status &= ~(1ULL << 8);               /* SPP=U */
    F->Rflags = Status;
    F->Vec = VEC_SYSCALL;
}

void HalTlsSetBase(UINT64 UserTlsBase) {
    /* tp = x4；同时靠帧恢复保持跨 trap */
    __asm__ volatile("mv tp, %0" :: "r"(UserTlsBase) : "memory");
}

void HalFrameSetTls(HAL_INTERRUPT_FRAME *F, UINT64 UserTlsBase) {
    if (F) {
        F->X[4] = UserTlsBase;
    }
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

/* Linux rv64：a7=号，a0..a2=参数，返回 a0 */
UINT64 HalFrameSyscallNum(const HAL_INTERRUPT_FRAME *F) {
    return F ? F->X[17] : 0;
}

UINT64 HalFrameGetArgument0(const HAL_INTERRUPT_FRAME *F) {
    return F ? F->X[10] : 0;
}

UINT64 HalFrameGetArgument1(const HAL_INTERRUPT_FRAME *F) {
    return F ? F->X[11] : 0;
}

UINT64 HalFrameGetArgument2(const HAL_INTERRUPT_FRAME *F) {
    return F ? F->X[12] : 0;
}

void HalFrameSetReturn(HAL_INTERRUPT_FRAME *F, UINT64 Value) {
    if (F) {
        F->X[10] = Value;
    }
}

void HalFrameSetReturn2(HAL_INTERRUPT_FRAME *F, UINT64 A, UINT64 B) {
    if (F) {
        F->X[10] = A;
        F->X[11] = B;
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
        F->X[10] = Value;
    }
}

void HalFrameSetArgument1(HAL_INTERRUPT_FRAME *F, UINT64 Value) {
    if (F) {
        F->X[11] = Value;
    }
}

/* RiscV：a0=sig，ra=返回点，sepc=handler */
int HalFrameSignalSetup(HAL_INTERRUPT_FRAME *F, UINT64 Handler, UINT64 Sig,
                        UINT64 *OutResumeIp, UINT64 *OutPushSp) {
    if (!F || Handler < 2) {
        return -1;
    }
    (void)OutResumeIp;
    (void)OutPushSp;
    F->X[1] = F->InstructionPointer;
    F->X[10] = Sig;
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
        "mv sp, %0\n"
        "jr %1\n"
        :
        : "r"(Stack), "r"(Entry)
        : "memory");
    HalVirtPlatformIdleLoop();
}
/* HalUserEnter 在 TrapVec.S */

/* 分页实现见 PageTable.c（PR-A7） */
