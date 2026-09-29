/*
 * HalFrame.c — PR-S3-hal-x64-1：中断帧 / TLS / 用户进入
 */
#include "Hal.h"
#include "Arch.h"

static void FrameZero(HAL_INTERRUPT_FRAME *F) {
    UINTN j;
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
    F->Cs = 0x08;   /* 内核代码段 */
    F->Rflags = 0x202;
    F->StackPointer = StackTop;
    F->Ss = 0x10;   /* 内核数据段 */
    F->Vector = VEC_TIMER;
    F->ErrorCode = 0;
}

void HalFrameSetUserEntry(HAL_INTERRUPT_FRAME *F, UINT64 Entry, UINT64 UserStackTop) {
    if (!F) {
        return;
    }
    FrameZero(F);
    F->InstructionPointer = Entry;
    F->Cs = 0x23;   /* 用户代码段 */
    F->Rflags = 0x202;
    F->StackPointer = UserStackTop;
    F->Ss = 0x1B;   /* 用户数据段 */
    F->Vector = VEC_TIMER;
    F->ErrorCode = 0;
}

void HalSetTlsBase(UINT64 UserTlsBase) {
    UINT32 Lo = (UINT32)UserTlsBase;
    UINT32 Hi = (UINT32)(UserTlsBase >> 32);

    /* IA32_FS_BASE — 用户 FS；与 KERNEL_GS（swapgs）无关 */
    __asm__ volatile("wrmsr" :: "c"(0xC0000100u), "a"(Lo), "d"(Hi) : "memory");
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

UINT64 HalFrameSyscallNum(const HAL_INTERRUPT_FRAME *F) {
    return F ? F->Rax : 0;
}

UINT64 HalFrameGetArgument0(const HAL_INTERRUPT_FRAME *F) {
    return F ? F->Rdi : 0;
}

UINT64 HalFrameGetArgument1(const HAL_INTERRUPT_FRAME *F) {
    return F ? F->Rsi : 0;
}

UINT64 HalFrameGetArgument2(const HAL_INTERRUPT_FRAME *F) {
    return F ? F->Rdx : 0;
}

void HalFrameSetReturn(HAL_INTERRUPT_FRAME *F, UINT64 Value) {
    if (F) {
        F->Rax = Value;
    }
}

void HalFrameSetReturn2(HAL_INTERRUPT_FRAME *F, UINT64 A, UINT64 B) {
    if (F) {
        F->Rax = A;
        F->Rdx = B;
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
        F->Rdi = Value;
    }
}

void HalFrameSetArgument1(HAL_INTERRUPT_FRAME *F, UINT64 Value) {
    if (F) {
        F->Rsi = Value;
    }
}

/* x86：压返回地址到用户栈，形如 call；入口 RSP≡8 (mod 16) */
int HalFrameSignalSetup(HAL_INTERRUPT_FRAME *F, UINT64 Handler, UINT64 Sig,
                        UINT64 *OutResumeIp, UINT64 *OutPushSp) {
    UINT64 OldIp;
    UINT64 Sp;
    UINT64 NewSp;

    if (!F || Handler < 2 || !OutResumeIp || !OutPushSp) {
        return -1;
    }
    OldIp = F->InstructionPointer;
    Sp = F->StackPointer;
    NewSp = (Sp & ~0xFULL) - 8;
    F->InstructionPointer = Handler;
    F->Rdi = Sig;
    *OutResumeIp = OldIp;
    *OutPushSp = NewSp;
    return 1;
}

UINT64 HalInterruptDispatch(struct HAL_INTERRUPT_FRAME *Frame) {
    return InterruptDispatch(Frame);
}

void HalSchedulerEnter(struct HAL_INTERRUPT_FRAME *Frame) {
    SchedulerEnter(Frame);
}

void HalUserEnter(struct HAL_INTERRUPT_FRAME *Frame) {
    UserEnter(Frame);
}

void HalUserCoopEnter(UINT64 Ksp, struct HAL_INTERRUPT_FRAME *Frame) {
    (void)Ksp;
    (void)Frame;
}

void HalUserCoopReturn(void) {
}
