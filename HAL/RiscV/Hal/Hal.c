/*
 * HAL/RiscV/Hal/Hal.c — PR-S3-hal-riscv-1：CPU / IRQ / Timer / 用户地址布局
 *
 * 中断帧见 HalFrame.c；平台元信息见 HalPlat.c（同构 x64/arm）。
 */
#include "Hal.h"

/*
 * OpenSBI 下内核在 S-mode：不可直接读/写 CLINT mtime（M-mode MMIO → 异常复位环）。
 * 用 rdtime + SBI set_timer；QEMU virt 常见 10MHz。
 */
#define TIME_HZ 10000000ULL
#define SSTATUS_SIE  (1ULL << 1)
#define SSTATUS_SPIE (1ULL << 5)
#define SSTATUS_SPP  (1ULL << 8)
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

int HalPowerButtonPressed(void) {
    return 0;
}

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

void HalTimerInitialize(void) {
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
        HalTimerInitialize();
    }
    HalTrapVectorInstall();
    HalTimerSetInterval(gTimerMs);
    __asm__ volatile("csrs sie, %0" ::"r"(SIE_STIE) : "memory");
    RiscvTimerArm();
    gTimerIrq = 1;
    HalSerialWrite("timer: RiscV SBI timer irq\n");
}

UINT32 HalTicksPerSec(void) {
    return 1000; /* SBI timer 已按 ms 武装 */
}

/* PR-A14：AP 本地开 STIE + SBI timer */
void HalTimerStartAp(void) {
    extern void HalTrapVectorInstall(void);

    if (!gTimerReady) {
        HalTimerInitialize();
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
        HalCpuIncrementTicks();
        gTimeLast += gTimePeriod;
        Delta -= gTimePeriod;
    }
}

/* PR-A13：S-mode / U-mode 定时中断 */
void HalTimerIrq(void) {
    HalCpuIncrementTicks();
    HalTimerAck();
}

void HalInstallUserMode(void) {
    UINT64 Status;

    extern void HalTrapVectorInstall(void);
    HalTrapVectorInstall();
    __asm__ volatile("csrr %0, sstatus" : "=r"(Status));
    Status |= (1ULL << 18); /* SUM：S 态可访问 U 页 */
    __asm__ volatile("csrw sstatus, %0" :: "r"(Status));
}

void HalSyscallInit(void) {
    HalInstallUserMode();
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
UINT64 HalUserMmapBase(void) {
    return 0x100200000ULL;
}
UINT64 HalUserMmapEnd(void) {
    return 0x100400000ULL;
}
UINT64 HalUserVirtEnd(void) {
    return HalUserMmapEnd();
}
