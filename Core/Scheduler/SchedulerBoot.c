/*
 * SchedulerBoot.c — idle、AP 与 SchedulerStart（PR-S-sched-1）
 */
#include "Scheduler.h"
#include "SchedulerPrivate.h"
#include "TaskFd.h"
#include "Syscall.h"
#include "Hal.h"
#include "Console.h"
#include "Debug.h"
#include "VirtualMemory.h"
#include "PhysicalMemory.h"
#include "SpinLock.h"
#include "ToySerialLog.h"

static int CreateIdleForCpu(UINT32 Cpu) {
    char Name[12];
    int Id;

    Name[0] = 'i';
    Name[1] = 'd';
    Name[2] = 'l';
    Name[3] = 'e';
    Name[4] = (char)('0' + (Cpu % 10));
    Name[5] = 0;
    Id = SchedulerCreate(Name, IdleTask);
    if (Id < 0) {
        return -1;
    }
    SchedulerSetAffinity(Id, (INT32)Cpu);
    SpinLockAcquire(&gSchedulerLock);
    gIdleSlot[Cpu] = Id;
    gTasks[Id].Priority = SCHED_PRIORITY_IDLE;
    RunQueueRemove(&gTasks[Id]);
    SpinLockRelease(&gSchedulerLock);
    return Id;
}

int SchedulerIsOnline(void) {
    return gSchedulerOnline;
}

void SchedulerApStart(void) {
    UINT32 Cpu;
    TASK *Idle;
    HAL_INTERRUPT_FRAME *Frame;

    Cpu = HalGetCpuId();
    while (!gSchedulerOnline) {
        HalCpuRelax();
    }
    SpinLockAcquire(&gSchedulerLock);
    Idle = IdleTaskForCpu(Cpu);
    if (!Idle) {
        SpinLockRelease(&gSchedulerLock);
        ToyLogSmp("sched: AP has no idle\n");
        for (;;) {
            HalCpuPark();
        }
    }
    /*
     * SmpBoot 在进本函数前已 sti。若此处仍 IF=1：OnTimer 会把 Idle->Frame
     * 改成 IRQ 栈帧，随后 HalSchedulerEnter 读到 SP=0 → #PF cr2=-8。
     * 首入前 cli，并用本地 Frame*（勿在解锁后再读 Idle->Frame）。
     */
    HalIrqDisable();
    Frame = Idle->Frame;
    if (!Frame || HalFrameGetStackPointer(Frame) < 0x10000ULL) {
        UINT8 *Top = Idle->Stack + sizeof(Idle->Stack);
        UINT8 *IrqCeil = Top - 8;
        UINT8 *IrqFloor = IrqCeil - sizeof(HAL_INTERRUPT_FRAME);
        Frame = (HAL_INTERRUPT_FRAME *)(IrqFloor - sizeof(HAL_INTERRUPT_FRAME));
        HalFrameSetKernelEntry(Frame, (UINT64)(UINTN)IdleTask,
                               (UINT64)(UINTN)Top);
        Idle->Frame = Frame;
        ToyLogSmp("sched: AP repaired idle frame\n");
    }
    ActivateTask(Idle);
    Idle->Started = 1;
    SpinLockRelease(&gSchedulerLock);
    /* 8 AP 并发写 COM1 会把欢迎语打成乱码；只留一条样例给冒烟 */
    if (Cpu == 1) {
        ToyLogSmp("sched: AP entered idle cpu=");
        ToyLogSmpHex32(Cpu);
        ToyLogSmp("\n");
    }
    HalSchedulerEnter(Frame);
    for (;;) {
        HalCpuPark();
    }
}

void SchedulerStart(void) {
    TASK *First = 0;
    int Cpus;
    int c;
    int i;

    Cpus = HalCpuCount();
    if (Cpus < 1) {
        Cpus = 1;
    }
    if (Cpus > HAL_MAX_CPUS) {
        Cpus = HAL_MAX_CPUS;
    }
    for (c = 0; c < Cpus; c++) {
        if (CreateIdleForCpu((UINT32)c) < 0) {
            ConsoleWrite("sched: idle create failed\n");
            for (;;) {
                HalCpuPark();
            }
        }
    }

    /*
     * PR-S-ap：多核时 shell/gui 同钉 AP（逻辑 CPU1），BSP 留给 idle0 / worker / 中断；
     * 单核仍钉 0。交互 Priority 偏高；worker 钉 BSP（rm-exc-11 与 INTERFACE 分核）。
     * PR-S-input-pin 序 2：input 钉独立 CPU2（SMP≥3），与 shell/gui 分核，sti 不外溢。
     * 串口 ConsoleOnly：shell 必须钉 BSP——virt RiscV/Arm64 定时器在 BSP，钉 AP 会永 WFI。
     */
    {
        UINT32 InteractiveCpu = (Cpus > 1) ? 1u : 0u;
        UINT32 InputCpu = (Cpus > 2) ? 2u : InteractiveCpu;

        if (HalConsoleOnly()) {
            InteractiveCpu = 0;
            InputCpu = 0;
        }

        for (i = 0; i < MAX_TASKS; i++) {
            if (gTasks[i].State == TASK_UNUSED) {
                continue;
            }
            if (gTasks[i].Name[0] == 'i' && gTasks[i].Name[1] == 'n') {
                /* input：钉专核，默认优先级（>idle -0x80，独占该核 drain） */
                RunQueueRemove(&gTasks[i]);
                gTasks[i].Affinity = (INT32)InputCpu;
                gTasks[i].HomeCpu = (INT32)InputCpu;
                gTasks[i].Priority = SCHED_PRIORITY_DEFAULT;
                RunQueueEnqueue(InputCpu, &gTasks[i]);
                continue;
            }
            if ((gTasks[i].Name[0] == 's' && gTasks[i].Name[1] == 'h') ||
                (gTasks[i].Name[0] == 'g' && gTasks[i].Name[1] == 'u')) {
                RunQueueRemove(&gTasks[i]);
                gTasks[i].Affinity = (INT32)InteractiveCpu;
                gTasks[i].HomeCpu = (INT32)InteractiveCpu;
                gTasks[i].Priority = SCHED_PRIORITY_SHELL;
                RunQueueEnqueue(InteractiveCpu, &gTasks[i]);
                continue;
            }
            /* worker：钉 BSP（≠ shell/gui 的 AP），后台 Job 与 INTERFACE 分核 */
            if (gTasks[i].Name[0] == 'w' && gTasks[i].Name[1] == 'o') {
                RunQueueRemove(&gTasks[i]);
                gTasks[i].Affinity = 0;
                gTasks[i].HomeCpu = 0;
                gTasks[i].Priority = SCHED_PRIORITY_DEFAULT;
                RunQueueEnqueue(0, &gTasks[i]);
            }
        }
    }

    First = 0;
    {
        TASK *Idle0 = IdleTaskForCpu(0);

        for (i = 0; i < MAX_TASKS; i++) {
            if (gTasks[i].State != TASK_READY) {
                continue;
            }
            if (Idle0 && &gTasks[i] == Idle0) {
                continue;
            }
            /* BSP 勿直接切入钉在 AP 上的任务 */
            if (gTasks[i].Affinity >= 0 && gTasks[i].Affinity != 0) {
                continue;
            }
            First = &gTasks[i];
            break;
        }
    }
    if (!First) {
        First = IdleTaskForCpu(0);
    }
    if (!First) {
        ConsoleWrite("sched: no tasks\n");
        for (;;) {
            HalCpuPark();
        }
    }

    HalIrqDisable();
    SpinLockAcquire(&gSchedulerLock);
    ActivateTask(First);
    First->Started = 1;
    gSchedulerOnline = 1;
    {
        HAL_INTERRUPT_FRAME *Frame = First->Frame;

        SpinLockRelease(&gSchedulerLock);
        HalTimerStart();
        DebugWrite("sched: online, entering tasks\n");
        HalSchedulerEnter(Frame);
    }
}

TASK *SchedulerCurrent(void) {
    return CurrentTask();
}

int SchedulerTaskCount(void) {
    return gTaskCount;
}


const TASK *SchedulerTaskByIndex(int Index) {
    if (Index < 0 || Index >= MAX_TASKS) {
        return 0;
    }
    if (gTasks[Index].State == TASK_UNUSED) {
        return 0;
    }
    return &gTasks[Index];
}

UINT64 SchedulerTaskRip(const TASK *T) {
    if (!T || !T->Frame) {
        return 0;
    }
    return HalFrameGetInstructionPointer(T->Frame);
}
