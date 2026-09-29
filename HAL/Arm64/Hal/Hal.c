/*
 * HAL/Arm64/Hal/Hal.c — PR-S3-hal-arm-1：CPU / IRQ / Timer / 用户地址布局
 *
 * 中断帧见 HalFrame.c；平台元信息见 HalPlat.c（同构 x64）。
 */
#include "Hal.h"
#include "Scheduler.h"
#include "BoardConfig.h"

static UINT64 gCntLast;
static UINT64 gCntFreq;
static UINT64 gCntPeriod;
static UINT32 gTimerMs = 10;
static int gTimerReady;
static int gTimerIrq;

extern void HalExceptionVectorsInstall(void);
extern void HalUserEnter(struct HAL_INTERRUPT_FRAME *Frame);
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

int HalPowerButtonPressed(void) {
    return 0;
}

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

void HalTimerInitialize(void) {
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
        HalTimerInitialize();
    }
    HalExceptionVectorsInstall();
    HalGicInit();
    HalTimerSetInterval(gTimerMs);
    ArmTimerArm();
    gTimerIrq = 1;
    HalSerialWrite("timer: Arm64 CNTV+GIC irq\n");
}

UINT32 HalTicksPerSec(void) {
    return 1000; /* CNTV 已按 ms 武装 */
}

/* PR-A14：AP 在 BSP HalTimerStart 之前也可本地开 CNTV（gCntPeriod 已在 InitCpu） */
void HalTimerStartAp(void) {
    extern void HalGicInitCpu(void);

    if (!gTimerReady) {
        HalTimerInitialize();
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
        HalCpuIncrementTicks();
        gCntLast += gCntPeriod;
        Delta -= gCntPeriod;
    }
}

/* PR-A13 / PR-V-input-fix：IRQ 帧上 tick + SchedulerOnTimer（对齐 x86） */
UINT64 HalExceptionIrqDispatch(HAL_INTERRUPT_FRAME *Frame) {
    UINT32 Id = HalGicAck();
    UINT64 Ret = 0;

    if (HalGicIsTimer(Id)) {
        HalCpuIncrementTicks();
        HalTimerAck();
        HalGicEoi(Id);
        if (SchedulerIsOnline() && Frame) {
            Ret = SchedulerOnTimer(Frame);
        }
        return Ret;
    }
    HalGicEoi(Id);
    return 0;
}

void HalInstallUserMode(void) {
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
UINT64 HalUserMmapBase(void) {
    return 0x100200000ULL;
}
UINT64 HalUserMmapEnd(void) {
    return 0x100400000ULL;
}
UINT64 HalUserVirtEnd(void) {
    return HalUserMmapEnd();
}
