/*
 * HAL/x86_64/Hal.c — x86-64 HAL 实现（委托给 Arch / VirtualMemory / Serial）
 */
#include "Hal.h"
#include "BootInfo.h"
#include "Arch.h"
#include "Debug.h"

int HalInit(void) {
    return ArchInit();
}

void HalCpuHalt(void) {
    /* 唤醒后保持 IF=1：若 hlt 后 cli，Shell 后续 NetPoll/绘屏整段关中断，
     * USB MSI 只在极短 hlt 窗口投递，宿主忙或 -smp 2 时易丢键鼠。 */
    __asm__ volatile ("sti; hlt" ::: "memory");
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

void HalInstallUserMode(void) {
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
UINT64 HalUserMmapBase(void) {
    return 0x40200000ULL;
}
UINT64 HalUserMmapEnd(void) {
    return 0x40400000ULL; /* 2MiB 教学匿名区 */
}
UINT64 HalUserVirtEnd(void) {
    return HalUserMmapEnd();
}
void HalUserSelfTest(void) {
    /* x86 用户路径由 FAT ELF / runuser 覆盖；无需内嵌自测 */
}

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

void HalDebugWriteHex32(UINT32 Value) {
    char Buf[12];

    HalSerialFormatHex(Buf, Value, 8);
    HalSerialWrite(Buf);
}

void HalDebugHex64(UINT64 Value) {
    char Buf[20];

    HalSerialFormatHex(Buf, Value, 16);
    HalSerialWrite(Buf);
}

void HalCpuPark(void) {
    __asm__ volatile ("cli; hlt");
}

int HalHasFrameBuffer(void) {
    const BOOT_INFO *Info = BootInfoGet();

    return (Info != 0 && Info->FrameBufferSize != 0) ? 1 : 0;
}

int HalConsoleOnly(void) {
    /* x86 课堂 / 真机桌面路径：始终走全量表，不用串口子集 */
    return 0;
}

int HalPlatformIsVirtSerialConsole(void) {
    return 0;
}

int HalCpuIsHypervisor(void) {
    UINT32 Eax;
    UINT32 Ebx;
    UINT32 Ecx;
    UINT32 Edx;

    __asm__ volatile("cpuid"
                     : "=a"(Eax), "=b"(Ebx), "=c"(Ecx), "=d"(Edx)
                     : "a"(1)
                     : "memory");
    (void)Eax;
    (void)Ebx;
    (void)Edx;
    return (Ecx & (1u << 31)) != 0;
}

void HalVirtPlatformIdleLoop(void) {
    for (;;) {
        HalCpuPark();
    }
}

void HalTimerPoll(void) {
}

int HalCpuIsHypervisor(void) {
    UINT32 Eax;
    UINT32 Ebx;
    UINT32 Ecx;
    UINT32 Edx;

    __asm__ volatile("cpuid"
                     : "=a"(Eax), "=b"(Ebx), "=c"(Ecx), "=d"(Edx)
                     : "a"(1)
                     : "memory");
    (void)Eax;
    (void)Ebx;
    (void)Edx;
    return (Ecx & (1u << 31)) != 0;
}

/* SmpBoot.c 提供 HalCpuCount / HalGetCpuId / HalSmpStartApplicationProcessors */

void HalSmpNoteDtb(UINT64 DtbPhys) {
    (void)DtbPhys;
}
