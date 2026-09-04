/*
 * HAL/riscv/Hal.c — RISC-V HAL（PR-A7 可链接；PR-A13 SBI timer IRQ）
 */
#include "Hal.h"

/*
 * OpenSBI 下内核在 S-mode：不可直接读/写 CLINT mtime（M-mode MMIO → 异常复位环）。
 * 用 rdtime + SBI set_timer；QEMU virt 常见 10MHz。
 */
#define TIME_HZ 10000000ULL
#define SSTATUS_SIE (1ULL << 1)
#define SIE_STIE    (1ULL << 5)
#define SBI_EXT_TIME 0x54494D45ULL
#define SBI_EXT_LEGACY_SET_TIMER 0x00ULL

static UINT64 gTimeLast;
static UINT64 gTimePeriod;
static UINT32 gTimerMs = 10;
static int gTimerReady;
static int gTimerIrq;

static UINT64 ReadTime(void) {
    UINT64 V;
    __asm__ volatile("rdtime %0" : "=r"(V));
    return V;
}

static void SbiSetTimer(UINT64 Next) {
    register UINT64 A0 __asm__("a0") = Next;
    register UINT64 A6 __asm__("a6") = 0; /* fid */
    register UINT64 A7 __asm__("a7") = SBI_EXT_TIME;
    __asm__ volatile("ecall"
                     : "+r"(A0)
                     : "r"(A6), "r"(A7)
                     : "memory", "a1");
    /* 若 TIME 扩展不可用，回退 legacy set_timer */
    if ((INT64)A0 < 0) {
        A0 = Next;
        A7 = SBI_EXT_LEGACY_SET_TIMER;
        A6 = 0;
        __asm__ volatile("ecall" : "+r"(A0) : "r"(A6), "r"(A7) : "memory", "a1");
    }
}

static void RiscvTimerArm(void) {
    UINT64 Next = ReadTime() + gTimePeriod;
    SbiSetTimer(Next);
}

int HalInit(void) {
    /* PR-A14：BSP 逻辑 CPU=0（tp） */
    __asm__ volatile("mv tp, zero" ::: "memory");
    return 0;
}

void HalCpuHalt(void) {
    /* PR-A13：开 SIE + WFI，由 SBI timer 唤醒 */
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
    __asm__ volatile("csrs sstatus, %0" ::"r"(SSTATUS_SIE) : "memory");
}
void HalIrqDisable(void) {
    __asm__ volatile("csrc sstatus, %0" ::"r"(SSTATUS_SIE) : "memory");
}
UINT64 HalIrqSave(void) {
    UINT64 Status;
    __asm__ volatile("csrr %0, sstatus" : "=r"(Status));
    HalIrqDisable();
    return Status;
}
void HalIrqRestore(UINT64 Flags) {
    if (Flags & SSTATUS_SIE) {
        HalIrqEnable();
    } else {
        HalIrqDisable();
    }
}
void HalCpuRelax(void) {
    __asm__ volatile("" ::: "memory");
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
    gTimePeriod = TIME_HZ / 100; /* ~10ms */
    if (gTimePeriod == 0) {
        gTimePeriod = 1;
    }
    gTimeLast = ReadTime();
    gTimerReady = 1;
}

void HalTimerSetInterval(UINT32 Milliseconds) {
    if (Milliseconds == 0) {
        Milliseconds = 10;
    }
    gTimerMs = Milliseconds;
    gTimePeriod = (TIME_HZ * (UINT64)Milliseconds) / 1000ULL;
    if (gTimePeriod == 0) {
        gTimePeriod = 1;
    }
}

void HalTimerAck(void) {
    RiscvTimerArm();
}

void HalTimerStart(void) {
    extern void HalTrapVectorInstall(void);

    if (!gTimerReady) {
        HalTimerInit();
    }
    HalTrapVectorInstall();
    HalTimerSetInterval(gTimerMs);
    __asm__ volatile("csrs sie, %0" ::"r"(SIE_STIE) : "memory");
    RiscvTimerArm();
    gTimerIrq = 1;
    HalSerialWrite("timer: RiscV SBI timer irq\n");
}

/* PR-A14：AP 本地开 STIE + SBI timer */
void HalTimerStartAp(void) {
    extern void HalTrapVectorInstall(void);

    if (!gTimerReady) {
        HalTimerInit();
    }
    HalTrapVectorInstall();
    __asm__ volatile("csrs sie, %0" ::"r"(SIE_STIE) : "memory");
    RiscvTimerArm();
    gTimerIrq = 1;
}

void HalTimerPoll(void) {
    UINT64 Now;
    UINT64 Delta;

    if (!gTimerReady || gTimerIrq) {
        return;
    }
    Now = ReadTime();
    Delta = Now - gTimeLast;
    while (Delta >= gTimePeriod) {
        HalCpuTickInc();
        gTimeLast += gTimePeriod;
        Delta -= gTimePeriod;
    }
}

/* PR-A13：S-mode / U-mode 定时中断 */
void HalTimerIrq(void) {
    HalCpuTickInc();
    HalTimerAck();
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
    UINT64 Status;

    extern void HalTrapVectorInstall(void);
    HalTrapVectorInstall();
    __asm__ volatile("csrr %0, sstatus" : "=r"(Status));
    Status |= (1ULL << 18); /* SUM：S 态可访问 U 页 */
    __asm__ volatile("csrw sstatus, %0" :: "r"(Status));
}

void HalSyscallInit(void) {
    HalUserInstall();
    HalSerialWrite("syscall: RiscV ecall (U-mode) ready\n");
    HalUserSelfTest();
}

void HalSetKernelStack(UINT64 StackTop) {
    /* U 态 sscratch=内核栈顶；此处供调度路径调用 */
    __asm__ volatile("csrw sscratch, %0" :: "r"(StackTop));
}

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
}

void HalFrameSetUserEntry(HAL_FRAME *F, UINT64 Entry, UINT64 UserRsp) {
    UINT64 Status;

    if (!F) {
        return;
    }
    FrameZero(F);
    F->Rip = Entry;
    F->Rsp = UserRsp;
    __asm__ volatile("csrr %0, sstatus" : "=r"(Status));
    Status |= (1ULL << 18) | (1ULL << 5); /* SUM|SPIE */
    Status &= ~(1ULL << 8);               /* SPP=U */
    F->Rflags = Status;
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

/* Linux rv64：a7=号，a0..a2=参数，返回 a0 */
UINT64 HalFrameSyscallNum(const HAL_FRAME *F) {
    return F ? F->X[17] : 0;
}

UINT64 HalFrameArg0(const HAL_FRAME *F) {
    return F ? F->X[10] : 0;
}

UINT64 HalFrameArg1(const HAL_FRAME *F) {
    return F ? F->X[11] : 0;
}

UINT64 HalFrameArg2(const HAL_FRAME *F) {
    return F ? F->X[12] : 0;
}

void HalFrameSetReturn(HAL_FRAME *F, UINT64 Value) {
    if (F) {
        F->X[10] = Value;
    }
}

void HalFrameSetReturn2(HAL_FRAME *F, UINT64 A, UINT64 B) {
    if (F) {
        F->X[10] = A;
        F->X[11] = B;
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
        "mv sp, %0\n"
        "jr %1\n"
        :
        : "r"(Stack), "r"(Entry)
        : "memory");
    HalVirtIdleLoop();
}
/* HalUserEnter 在 TrapVec.S */

/* 分页实现见 Page.c（PR-A7） */

const char *HalArchName(void) { return "riscv64"; }
const char *HalCpuInfo(void) { return "RISC-V (virt A14)"; }

UINT16 HalElfMachine(void) {
    return 243; /* EM_RISCV */
}

/* PR-A12：RISC-V ELF reloc → Common Elf.c 可处理 kind */
HAL_ELF_RELOC_KIND HalElfRelocKind(UINT32 Type) {
    switch (Type) {
    case 3: /* R_RISCV_RELATIVE */
        return HAL_ELF_RELOC_RELATIVE;
    case 2: /* R_RISCV_64 */
        return HAL_ELF_RELOC_ABS64;
    case 5: /* R_RISCV_JUMP_SLOT */
        return HAL_ELF_RELOC_JUMP_SLOT;
    case 4: /* R_RISCV_COPY */
        return HAL_ELF_RELOC_COPY;
    default:
        return HAL_ELF_RELOC_UNSUPPORTED;
    }
}

void HalSyncICache(void *Addr, UINTN Size) {
    (void)Addr;
    (void)Size;
    __asm__ volatile("fence.i" ::: "memory");
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
