/*
 * Scheduler.c — 任务创建与切换（PR-S-sched-1）
 * 信号：SchedulerSignal.c；fork/kill：SchedulerUser.c；启动：SchedulerBoot.c
 */
#include "Scheduler.h"
#include "SchedulerOps.h"
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
/* 5k：idle 用槽位索引，避免 IF=1 下裸指针被砸成 PickNext bad idle */
INT32 gIdleSlot[HAL_MAX_CPUS];
SPIN_LOCK gSchedulerLock;           /* 任务槽 / fork·exit·wait·kill */
volatile int gSchedulerOnline;
int gTaskCount;

TASK *IdleTaskForCpu(UINT32 Cpu) {
    INT32 S;

    if (Cpu >= HAL_MAX_CPUS) {
        return 0;
    }
    S = gIdleSlot[Cpu];
    if (S < 0 || (UINT32)S >= (UINT32)MAX_TASKS) {
        return 0;
    }
    return &gTasks[S];
}

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
    INT32 S;

    if (!T) {
        return 0;
    }
    for (c = 0; c < HAL_MAX_CPUS; c++) {
        S = gIdleSlot[c];
        if (S >= 0 && (UINT32)S < (UINT32)MAX_TASKS && &gTasks[S] == T) {
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
    SchedulerOpsRegister(SchedulerRoundRobinOps());
    RunQueueInitialize();
    SchedulerOpsGet()->Init();
    for (c = 0; c < HAL_MAX_CPUS; c++) {
        gCurrentCpu[c] = 0;
        gIdleSlot[c] = -1;
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
        gTasks[i].SleepWakeTick = 0;
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
        gTasks[i].Cwd[0] = 0;
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

/* PR-GUI-kerneltask：CreateKernel 的入口。Fn/Ctx 在入队前写入槽位。 */
static void (*gKernFn[MAX_TASKS])(void *);
static void *gKernCtx[MAX_TASKS];
static void (*gPendFn)(void *);
static void *gPendCtx;

static void KernelCtxEntry(void) {
    TASK *T = CurrentTask();
    int Id = (int)(T - gTasks);
    void (*Fn)(void *) = 0;
    void *Ctx = 0;

    if (Id >= 0 && Id < MAX_TASKS) {
        Fn = gKernFn[Id];
        Ctx = gKernCtx[Id];
    }
    if (Fn) {
        Fn(Ctx);
    }
    for (;;) {
        HalCpuHalt();
    }
}

int SchedulerCreate(const char *Name, void (*Entry)(void)) {
    void (*Use)(void) = Entry;
    void (*PendFn)(void *) = 0;
    void *PendCtx = 0;

    SpinLockAcquire(&gSchedulerLock);
    if (gPendFn) {
        PendFn = gPendFn;
        PendCtx = gPendCtx;
        gPendFn = 0;
        gPendCtx = 0;
        Use = KernelCtxEntry;
    }
    for (int i = 0; i < MAX_TASKS; i++) {
        if (gTasks[i].State != TASK_UNUSED) {
            continue;
        }
        UINT8 *Top = gTasks[i].Stack + sizeof(gTasks[i].Stack);
        /*
         * rm-exc-10：X64 KernelEnter 后 RSP=Top-8；首 IRQ 帧占 [Top-184, Top-8)。
         * 5l Create@Top-176 与之重叠（且 F->Ss≡Top-8 被伪返回清零）→ remove 期
         * #GP@IsrCommon iretq（err 垃圾选择子）。伪返回槽 + 整帧红区 + Create 在下。
         * 勿只挪 8（刀 8 ❌）；CreateUser/fork 仍 Top-sizeof（走 UserEnter/已 Started）。
         */
        {
            UINT8 *IrqCeil = Top - 8;
            UINT8 *IrqFloor = IrqCeil - sizeof(HAL_INTERRUPT_FRAME);
            HAL_INTERRUPT_FRAME *F =
                (HAL_INTERRUPT_FRAME *)(IrqFloor - sizeof(HAL_INTERRUPT_FRAME));
            HalFrameSetKernelEntry(F, (UINT64)(UINTN)Use, (UINT64)(UINTN)Top);
            gTasks[i].Frame = F;
        }
        gTasks[i].State = TASK_READY;
        gTasks[i].Ticks = 0;
        gTasks[i].PageRoot = VirtualMemoryKernelRoot();
        gTasks[i].IsUser = 0;
        gTasks[i].Started = 0;
        gTasks[i].UserSpace = 0;
        gTasks[i].ParentId = -1;
        gTasks[i].ExitCode = 0;
        gTasks[i].Waiting = 0;
        gTasks[i].SleepWakeTick = 0;
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
        if (PendFn) {
            gKernFn[i] = PendFn;
            gKernCtx[i] = PendCtx;
        }
        gTaskCount++;
        {
            UINT32 Home = SchedulerOpsGet()->PickHome(&gTasks[i]);
            RunQueueEnqueue(Home, &gTasks[i]);
        }
        SpinLockRelease(&gSchedulerLock);
        return i;
    }
    SpinLockRelease(&gSchedulerLock);
    return -1;
}

int SchedulerCreateKernel(const char *Name, void (*Fn)(void *), void *Ctx) {
    if (!Name || !Fn) {
        return -1;
    }
    gPendFn = Fn;
    gPendCtx = Ctx;
    return SchedulerCreate(Name, KernelCtxEntry);
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
        gTasks[i].SleepWakeTick = 0;
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
        gTasks[i].Cwd[0] = 0;
        TaskClearFds(&gTasks[i]);
        CopyName(&gTasks[i], Name);
        gTaskCount++;
        {
            UINT32 Home = SchedulerOpsGet()->PickHome(&gTasks[i]);
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
        UINT32 Home = (T->HomeCpu >= 0) ? (UINT32)T->HomeCpu : SchedulerOpsGet()->PickHome(T);
        SchedulerOpsGet()->Remove(T);
        RunQueueEnqueue(Home, T);
    }
    SpinLockRelease(&gSchedulerLock);
    return 0;
}

UINT64 SchedulerResumeFrame(TASK *T) {
    UINT64 Frame = (UINT64)(UINTN)T->Frame;
    UINT64 Sp;

    if (Frame == 0) {
        return 0;
    }
    if (T->Started) {
        return Frame;
    }
    /* 首入 KernelEnter：StackPointer 须为 Create 栈顶；损坏则就地修复 */
    Sp = HalFrameGetStackPointer(T->Frame);
    if (Sp < 0x10000ULL) {
        UINT8 *Top = T->Stack + sizeof(T->Stack);
        UINT64 Ip = HalFrameGetInstructionPointer(T->Frame);

        if (Ip == 0) {
            ToyLogSmp("sched: bad StackPointer no ip\n");
            return 0;
        }
        HalFrameSetKernelEntry(T->Frame, Ip, (UINT64)(UINTN)Top);
        ToyLogSmp("sched: repaired StackPointer\n");
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
    SchedulerOpsGet()->Remove(T);
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

