/*
 * HAL/x86_64/Hal.c — x86-64 HAL 实现（委托给 Arch / VirtualMemory / Serial）
 */
#include "Hal.h"
#include "Arch.h"
#include "Debug.h"

int HalInit(void) {
    return ArchInit();
}

void HalCpuHalt(void) {
    __asm__ volatile ("sti; hlt; cli" ::: "memory");
}

void HalCpuReboot(void) {
    UINT32 i;

    HalIrqDisable();
    /* 8042 脉冲复位（QEMU/PC 通用）；失败则 CF9 冷复位 */
    for (i = 0; i < 100000; i++) {
        if ((HalIoRead8(0x64) & 0x02) == 0) {
            break;
        }
    }
    HalIoWrite8(0x64, 0xFE);
    HalIoWrite8(0xCF9, 0x06);
    for (;;) {
        HalCpuHalt();
    }
}

void HalCpuShutdown(void) {
    for (;;) {
        __asm__ volatile ("cli; hlt");
    }
}

void HalIrqEnable(void) {
    ArchSti();
}

void HalIrqDisable(void) {
    ArchCli();
}

UINT64 HalIrqSave(void) {
    UINT64 Flags;

    __asm__ volatile ("pushfq; pop %0; cli" : "=r"(Flags) :: "memory");
    return Flags;
}

void HalIrqRestore(UINT64 Flags) {
    if (Flags & (1ULL << 9)) {
        ArchSti();
    } else {
        ArchCli();
    }
}

void HalCpuRelax(void) {
    __asm__ volatile ("pause" ::: "memory");
}

void HalIrqVectorSet(UINT32 Vector, void *Handler, UINT8 Type) {
    ArchIdtSetGate(Vector, Handler, Type);
}

void HalIrqRegister(UINT32 Vector, void (*Handler)(void)) {
    ArchIdtSetGate(Vector, Handler, 0x8E);
}

void HalIrqUnregister(UINT32 Vector) {
    ArchIdtSetGate(Vector, (void *)0, 0);
    (void)Vector;
}

void HalIrqEoi(UINT32 Vector) {
    (void)Vector;
    LapicEoi();
}

void HalTimerInit(void) {
}

void HalTimerSetInterval(UINT32 Milliseconds) {
    (void)Milliseconds;
}

void HalTimerAck(void) {
    LapicEoi();
}

void HalTimerStart(void) {
    TimerStart();
}

void HalUserInstall(void) {
    ArchTssInstall();
}

void HalSyscallInit(void) {
    extern void Isr128(void);

    /* legacy：IDT 0x80，DPL=3（不进 Common） */
    ArchIdtSetGate(VEC_SYSCALL, (void *)Isr128, 0xEE);
    DebugWrite("syscall: vector 0x80 (DPL=3) ready\n");
    /* 快速路径 MSR（BSP）；AP 在 ArchApInit 中各自初始化 */
    ArchSyscallMsrInit(0);
    DebugWrite("syscall: SYSCALL/SYSRET MSR ready\n");
}

void HalSetKernelStack(UINT64 StackTop) {
    ArchSetRsp0(StackTop);
}

UINT64 HalUserCodeVirt(void) {
    return 0x40000000ULL;
}
UINT64 HalUserStackVirt(void) {
    return 0x40100000ULL;
}
UINT64 HalUserStackSize(void) {
    return 0x4000ULL;
}
UINT64 HalUserBrkMax(void) {
    return 0x40080000ULL;
}
UINT64 HalUserSoBase(void) {
    return 0x40080000ULL;
}
UINT64 HalUserVirtEnd(void) {
    return HalUserStackVirt() + HalUserStackSize();
}
void HalUserSelfTest(void) {
    /* x86 用户路径由 FAT ELF / runuser 覆盖；无需内嵌自测 */
}

static void FrameZero(HAL_FRAME *F) {
    UINTN j;
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
    F->Cs = 0x08;   /* 内核代码段 */
    F->Rflags = 0x202;
    F->Rsp = StackTop;
    F->Ss = 0x10;   /* 内核数据段 */
    F->Vector = VEC_TIMER;
    F->ErrorCode = 0;
}

void HalFrameSetUserEntry(HAL_FRAME *F, UINT64 Entry, UINT64 UserRsp) {
    if (!F) {
        return;
    }
    FrameZero(F);
    F->Rip = Entry;
    F->Cs = 0x23;   /* 用户代码段 */
    F->Rflags = 0x202;
    F->Rsp = UserRsp;
    F->Ss = 0x1B;   /* 用户数据段 */
    F->Vector = VEC_TIMER;
    F->ErrorCode = 0;
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

UINT64 HalFrameSyscallNum(const HAL_FRAME *F) {
    return F ? F->Rax : 0;
}

UINT64 HalFrameArg0(const HAL_FRAME *F) {
    return F ? F->Rdi : 0;
}

UINT64 HalFrameArg1(const HAL_FRAME *F) {
    return F ? F->Rsi : 0;
}

UINT64 HalFrameArg2(const HAL_FRAME *F) {
    return F ? F->Rdx : 0;
}

void HalFrameSetReturn(HAL_FRAME *F, UINT64 Value) {
    if (F) {
        F->Rax = Value;
    }
}

void HalFrameSetReturn2(HAL_FRAME *F, UINT64 A, UINT64 B) {
    if (F) {
        F->Rax = A;
        F->Rdx = B;
    }
}

UINT64 HalInterruptDispatch(struct HAL_FRAME *Frame) {
    return InterruptDispatch(Frame);
}

void HalSchedulerEnter(struct HAL_FRAME *Frame) {
    SchedulerEnter(Frame);
}

void HalUserEnter(struct HAL_FRAME *Frame) {
    UserEnter(Frame);
}

void HalUserCoopEnter(UINT64 Ksp, struct HAL_FRAME *Frame) {
    (void)Ksp;
    (void)Frame;
}

void HalUserCoopReturn(void) {
}

const char *HalArchName(void) {
    return "x86_64";
}

const char *HalCpuInfo(void) {
    return "x86-64 (ToyOS HAL)";
}

/* PR-A4：ELF e_machine = EM_X86_64 */
UINT16 HalElfMachine(void) {
    return 62;
}

HAL_ELF_RELOC_KIND HalElfRelocKind(UINT32 Type) {
    switch (Type) {
    case 8:  /* R_X86_64_RELATIVE */
        return HAL_ELF_RELOC_RELATIVE;
    case 1:  /* R_X86_64_64 */
        return HAL_ELF_RELOC_ABS64;
    case 6:  /* R_X86_64_GLOB_DAT */
        return HAL_ELF_RELOC_GLOB_DAT;
    case 7:  /* R_X86_64_JUMP_SLOT */
        return HAL_ELF_RELOC_JUMP_SLOT;
    case 5:  /* R_X86_64_COPY */
        return HAL_ELF_RELOC_COPY;
    default:
        return HAL_ELF_RELOC_UNSUPPORTED;
    }
}

void HalSyncICache(void *Addr, UINTN Size) {
    (void)Addr;
    (void)Size;
}

void HalDebugWrite(const char *Text) {
    HalSerialWrite(Text);
}

void HalDebugHex32(UINT32 Value) {
    char Buf[12];

    HalSerialHexFormat(Buf, Value, 8);
    HalSerialWrite(Buf);
}

void HalDebugHex64(UINT64 Value) {
    char Buf[20];

    HalSerialHexFormat(Buf, Value, 16);
    HalSerialWrite(Buf);
}

void HalCpuPark(void) {
    __asm__ volatile ("cli; hlt");
}

int HalPlatformVirtConsole(void) {
    return 0;
}

void HalVirtIdleLoop(void) {
    for (;;) {
        HalCpuPark();
    }
}

void HalTimerPoll(void) {
}

/* SmpBoot.c 提供 HalCpuCount / HalCpuId / HalSmpStartAps */

void HalSmpNoteDtb(UINT64 DtbPhys) {
    (void)DtbPhys;
}
