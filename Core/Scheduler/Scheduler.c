/*
 * Scheduler.c — 任务创建与切换（PR-S-sched-1）
 * 信号：SchedulerSignal.c；fork/kill：SchedulerUser.c；启动：SchedulerBoot.c
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

#define SCHED_TAG_KERNEL_FIRST 1ULL
#define SCHED_TAG_USER_FIRST   2ULL

TASK gTasks[MAX_TASKS];
static TASK *gCurrentCpu[HAL_MAX_CPUS];
TASK *gIdleTask[HAL_MAX_CPUS];
SPIN_LOCK gSchedulerLock;           /* 任务槽 / fork·exit·wait·kill */
volatile int gSchedulerOnline;
int gTaskCount;

TASK *CurrentTask(void) {
    UINT32 Id = HalGetCpuId();
    if (Id >= HAL_MAX_CPUS) {
        return 0;
    }
    return gCurrentCpu[Id];
}

void SetCurrentTask(TASK *T) {
    UINT32 Id = HalGetCpuId();
    if (Id < HAL_MAX_CPUS) {
        gCurrentCpu[Id] = T;
    }
}

int IsIdleTask(const TASK *T) {
    UINT32 c;
    if (!T) {
        return 0;
    }
    for (c = 0; c < HAL_MAX_CPUS; c++) {
        if (gIdleTask[c] == T) {
            return 1;
        }
    }
    return 0;
}

INT32 TaskSlot(const TASK *T) {
    if (!T) {
        return -1;
    }
    return (INT32)(T - gTasks);
}

/* FD/管道/套接字：见 TaskFd.c（PR-R3） */

void SchedulerInitialize(void) {
    int c;

    SpinLockInit(&gSchedulerLock);
    gSchedulerOnline = 0;
    RunQueueInitialize();
    for (c = 0; c < HAL_MAX_CPUS; c++) {
        gCurrentCpu[c] = 0;
        gIdleTask[c] = 0;
    }
    for (int i = 0; i < MAX_TASKS; i++) {
        gTasks[i].State = TASK_UNUSED;
        gTasks[i].Frame = 0;
        gTasks[i].Id = (UINT32)i;
        gTasks[i].Ticks = 0;
        gTasks[i].Name[0] = 0;
        gTasks[i].PageRoot = 0;
        gTasks[i].IsUser = 0;
        gTasks[i].Started = 0;
        gTasks[i].UserSpace = 0;
        gTasks[i].ParentId = -1;
        gTasks[i].ExitCode = 0;
        gTasks[i].Waiting = 0;
        gTasks[i].PendingKill = 0;
        gTasks[i].SigHandlerInt = 0;
        gTasks[i].SigHandlerTerm = 0;
        gTasks[i].Affinity = -1;
        gTasks[i].OnCpu = -1;
        gTasks[i].HomeCpu = 0;
        gTasks[i].Priority = SCHED_PRIORITY_DEFAULT;
        gTasks[i].InRunQueue = 0;
        gTasks[i].BrkBase = 0;
        gTasks[i].Brk = 0;
        gTasks[i].MmapNext = 0;
        TaskClearFds(&gTasks[i]);
    }
    gTaskCount = 0;
}

void CopyName(TASK *T, const char *Name) {
    int n = 0;
    while (Name[n] && n < 15) {
        T->Name[n] = Name[n];
        n++;
    }
    T->Name[n] = 0;
}

void IdleTask(void) {
    for (;;) {
        HalCpuHalt();
    }
}

int SchedulerCreate(const char *Name, void (*Entry)(void)) {
    SpinLockAcquire(&gSchedulerLock);
    for (int i = 0; i < MAX_TASKS; i++) {
        if (gTasks[i].State != TASK_UNUSED) {
            continue;
        }
        UINT8 *Top = gTasks[i].Stack + sizeof(gTasks[i].Stack);
        HAL_INTERRUPT_FRAME *F = (HAL_INTERRUPT_FRAME *)(Top - sizeof(HAL_INTERRUPT_FRAME));
        HalFrameSetKernelEntry(F, (UINT64)(UINTN)Entry, (UINT64)(UINTN)Top);

        gTasks[i].Frame = F;
        gTasks[i].State = TASK_READY;
        gTasks[i].Ticks = 0;
        gTasks[i].PageRoot = VirtualMemoryKernelRoot();
        gTasks[i].IsUser = 0;
        gTasks[i].Started = 0;
        gTasks[i].UserSpace = 0;
        gTasks[i].ParentId = -1;
        gTasks[i].ExitCode = 0;
        gTasks[i].Waiting = 0;
        gTasks[i].PendingKill = 0;
        gTasks[i].SigHandlerInt = 0;
        gTasks[i].SigHandlerTerm = 0;
        gTasks[i].Affinity = -1;
        gTasks[i].OnCpu = -1;
        gTasks[i].HomeCpu = 0;
        gTasks[i].Priority = SCHED_PRIORITY_DEFAULT;
        gTasks[i].InRunQueue = 0;
        TaskClearFds(&gTasks[i]);
        CopyName(&gTasks[i], Name);
        gTaskCount++;
        {
            UINT32 Home = PickHomeCpu(&gTasks[i]);
            RunQueueEnqueue(Home, &gTasks[i]);
        }
        SpinLockRelease(&gSchedulerLock);
        return i;
    }
    SpinLockRelease(&gSchedulerLock);
    return -1;
}

int SchedulerCreateUser(const char *Name, UINT64 Rip, UINT64 Rsp, UINT64 PageRoot,
                    VIRTUAL_ADDRESS_SPACE *Space, UINT64 BrkBase) {
    TASK *Cur;

    SpinLockAcquire(&gSchedulerLock);
    Cur = CurrentTask();
    for (int i = 0; i < MAX_TASKS; i++) {
        if (gTasks[i].State != TASK_UNUSED) {
            continue;
        }
        UINT8 *Top = gTasks[i].Stack + sizeof(gTasks[i].Stack);
        HAL_INTERRUPT_FRAME *F = (HAL_INTERRUPT_FRAME *)(Top - sizeof(HAL_INTERRUPT_FRAME));
        HalFrameSetUserEntry(F, Rip, Rsp);

        gTasks[i].Frame = F;
        gTasks[i].State = TASK_READY;
        gTasks[i].Ticks = 0;
        gTasks[i].PageRoot = PageRoot;
        gTasks[i].IsUser = 1;
        gTasks[i].Started = 0;
        gTasks[i].UserSpace = Space;
        gTasks[i].ParentId = Cur ? TaskSlot(Cur) : -1;
        gTasks[i].ExitCode = 0;
        gTasks[i].Waiting = 0;
        gTasks[i].PendingKill = 0;
        gTasks[i].SigHandlerInt = 0;
        gTasks[i].SigHandlerTerm = 0;
        gTasks[i].Affinity = 0; /* Console/串口非 SMP 安全；用户先钉 BSP */
        gTasks[i].OnCpu = -1;
        gTasks[i].HomeCpu = 0;
        /* 继承创建者优先级，避免 shell/gui(prio=8) 在 UP 上饿死用户(0) */
        gTasks[i].Priority = (Cur && !IsIdleTask(Cur)) ? Cur->Priority : SCHED_PRIORITY_DEFAULT;
        gTasks[i].InRunQueue = 0;
        gTasks[i].BrkBase = BrkBase;
        gTasks[i].Brk = BrkBase;
        gTasks[i].MmapNext = USER_MMAP_BASE;
        TaskClearFds(&gTasks[i]);
        CopyName(&gTasks[i], Name);
        gTaskCount++;
        {
            UINT32 Home = PickHomeCpu(&gTasks[i]);
            RunQueueEnqueue(Home, &gTasks[i]);
        }
        SpinLockRelease(&gSchedulerLock);
        return i;
    }
    SpinLockRelease(&gSchedulerLock);
    return -1;
}

void SchedulerSetAffinity(int TaskId, INT32 Cpu) {
    if (TaskId < 0 || TaskId >= MAX_TASKS) {
        return;
    }
    SpinLockAcquire(&gSchedulerLock);
    if (gTasks[TaskId].State != TASK_UNUSED) {
        gTasks[TaskId].Affinity = Cpu;
    }
    SpinLockRelease(&gSchedulerLock);
}

int SchedulerSetPriority(INT32 Pid, INT32 Priority) {
    int Slot;
    TASK *T;

    if (Pid <= 0 || Pid > MAX_TASKS) {
        return -1;
    }
    if (Priority < SCHED_PRIORITY_IDLE || Priority > 127) {
        return -1;
    }
    Slot = (int)(Pid - 1);
    SpinLockAcquire(&gSchedulerLock);
    T = &gTasks[Slot];
    if (T->State == TASK_UNUSED || IsIdleTask(T)) {
        SpinLockRelease(&gSchedulerLock);
        return -1;
    }
    T->Priority = Priority;
    /* 已在 READY 队列：重插以按新优先级排序 */
    if (T->State == TASK_READY && T->InRunQueue) {
        UINT32 Home = (T->HomeCpu >= 0) ? (UINT32)T->HomeCpu : PickHomeCpu(T);
        RunQueueRemove(T);
        RunQueueEnqueue(Home, T);
    }
    SpinLockRelease(&gSchedulerLock);
    return 0;
}

UINT64 SchedulerResumeFrame(TASK *T) {
    UINT64 Frame = (UINT64)(UINTN)T->Frame;

    if (Frame == 0) {
        return 0;
    }
    if (T->Started) {
        return Frame;
    }
    T->Started = 1;
    if (T->IsUser) {
        return Frame | SCHED_TAG_USER_FIRST;
    }
    return Frame | SCHED_TAG_KERNEL_FIRST;
}

/* Ring3 中断/系统调用走 TSS.RSP0；每用户任务必须用自己的内核栈 */
void ActivateTask(TASK *T) {
    UINT32 Cpu = HalGetCpuId();
    TASK *Prev = CurrentTask();

    if (Prev && Prev != T && Prev->State == TASK_RUNNING) {
        Prev->State = TASK_READY;
        Prev->OnCpu = -1;
        if (!IsIdleTask(Prev)) {
            RunQueueEnqueue(Cpu, Prev); /* 留在本核队列，利于缓存 */
        }
    }
    RunQueueRemove(T);
    SetCurrentTask(T);
    T->State = TASK_RUNNING;
    T->OnCpu = (INT32)Cpu;
    if (T->IsUser) {
        HalSetKernelStack((UINT64)(UINTN)(T->Stack + sizeof(T->Stack)));
    }
    if (T->PageRoot != 0) {
        VirtualMemoryLoadPageTable(T->PageRoot);
    }
}

