/*
 * Scheduler.c — 抢占式任务调度器（fork/wait/SMP；FD 见 TaskFd.c）
 */
#include "Scheduler.h"
#include "TaskFd.h"
#include "Syscall.h"
#include "Hal.h"
#include "Console.h"
#include "Debug.h"
#include "VirtualMemory.h"
#include "PhysicalMemory.h"
#include "SpinLock.h"

#define SCHED_TAG_KERNEL_FIRST 1ULL
#define SCHED_TAG_USER_FIRST   2ULL

static TASK gTasks[MAX_TASKS];
static TASK *gCurrentCpu[HAL_MAX_CPUS];
static TASK *gIdleTask[HAL_MAX_CPUS];
static SPIN_LOCK gSchedulerLock;           /* 任务槽 / fork·exit·wait·kill */
static SPIN_LOCK gRunqLock[HAL_MAX_CPUS];  /* PR-S-runq：每核 READY 队列 */
static volatile int gSchedulerOnline;
static int gTaskCount;
static volatile int gRoundRobinHome; /* 新建任务轮转 HomeCpu（原子自增） */
static volatile UINT64 gStealCount;  /* 偷任务次数（调试/ps） */

typedef struct {
    TASK *Slot[MAX_TASKS];
    int   Count;
} CPU_RUN_QUEUE;

static CPU_RUN_QUEUE gRunq[HAL_MAX_CPUS];

/*
 * 锁序（防死锁）：若同持两把 → 先 gSchedulerLock，再 gRunqLock；
 * 多把 gRunqLock → 按 cpu 下标升序。热路径（timer/yield）可只持 runq 锁。
 */

static TASK *CurrentTask(void) {
    UINT32 Id = HalGetCpuId();
    if (Id >= HAL_MAX_CPUS) {
        return 0;
    }
    return gCurrentCpu[Id];
}

static void SetCurrentTask(TASK *T) {
    UINT32 Id = HalGetCpuId();
    if (Id < HAL_MAX_CPUS) {
        gCurrentCpu[Id] = T;
    }
}

static int IsIdleTask(const TASK *T) {
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

static void RunqInit(void) {
    UINT32 c;
    int i;
    for (c = 0; c < HAL_MAX_CPUS; c++) {
        SpinLockInit(&gRunqLock[c]);
        gRunq[c].Count = 0;
        for (i = 0; i < MAX_TASKS; i++) {
            gRunq[c].Slot[i] = 0;
        }
    }
}

/* 调用方已持 gRunqLock[Cpu] */
static void RunqEnqueueLocked(UINT32 Cpu, TASK *T) {
    CPU_RUN_QUEUE *Q;
    int Pos;
    if (!T || Cpu >= HAL_MAX_CPUS || IsIdleTask(T) || T->InRunq) {
        return;
    }
    Q = &gRunq[Cpu];
    if (Q->Count >= MAX_TASKS) {
        return;
    }
    /* 高 Priority 靠前；同级 FIFO——队尾偷任务偏向低优先级 */
    Pos = Q->Count;
    while (Pos > 0 && Q->Slot[Pos - 1]->Priority < T->Priority) {
        Q->Slot[Pos] = Q->Slot[Pos - 1];
        Pos--;
    }
    Q->Slot[Pos] = T;
    Q->Count++;
    T->InRunq = 1;
    T->HomeCpu = (INT32)Cpu;
}

static void RunqEnqueue(UINT32 Cpu, TASK *T) {
    if (!T || Cpu >= HAL_MAX_CPUS) {
        return;
    }
    SpinLockAcquire(&gRunqLock[Cpu]);
    RunqEnqueueLocked(Cpu, T);
    SpinLockRelease(&gRunqLock[Cpu]);
}

static TASK *RunqDequeueLocked(UINT32 Cpu) {
    CPU_RUN_QUEUE *Q;
    TASK *T;
    int i;
    if (Cpu >= HAL_MAX_CPUS) {
        return 0;
    }
    Q = &gRunq[Cpu];
    if (Q->Count <= 0) {
        return 0;
    }
    T = Q->Slot[0];
    for (i = 1; i < Q->Count; i++) {
        Q->Slot[i - 1] = Q->Slot[i];
    }
    Q->Count--;
    Q->Slot[Q->Count] = 0;
    if (T) {
        T->InRunq = 0;
    }
    return T;
}

/* 从队尾偷：与本地队头 dequeue 错开，减冲突 */
static TASK *RunqStealOneLocked(UINT32 Victim) {
    CPU_RUN_QUEUE *Q;
    TASK *T;
    if (Victim >= HAL_MAX_CPUS) {
        return 0;
    }
    Q = &gRunq[Victim];
    if (Q->Count <= 0) {
        return 0;
    }
    T = Q->Slot[Q->Count - 1];
    Q->Count--;
    Q->Slot[Q->Count] = 0;
    if (T) {
        T->InRunq = 0;
    }
    return T;
}

static int RunqRemoveFromCpuLocked(UINT32 Cpu, TASK *T) {
    CPU_RUN_QUEUE *Q;
    int i, j;
    if (!T || Cpu >= HAL_MAX_CPUS) {
        return 0;
    }
    Q = &gRunq[Cpu];
    for (i = 0; i < Q->Count; i++) {
        if (Q->Slot[i] != T) {
            continue;
        }
        for (j = i + 1; j < Q->Count; j++) {
            Q->Slot[j - 1] = Q->Slot[j];
        }
        Q->Count--;
        Q->Slot[Q->Count] = 0;
        T->InRunq = 0;
        return 1;
    }
    return 0;
}

static void RunqRemove(TASK *T) {
    UINT32 c;
    if (!T || !T->InRunq) {
        return;
    }
    if (T->HomeCpu >= 0 && (UINT32)T->HomeCpu < HAL_MAX_CPUS) {
        c = (UINT32)T->HomeCpu;
        SpinLockAcquire(&gRunqLock[c]);
        if (RunqRemoveFromCpuLocked(c, T)) {
            SpinLockRelease(&gRunqLock[c]);
            return;
        }
        SpinLockRelease(&gRunqLock[c]);
    }
    for (c = 0; c < HAL_MAX_CPUS; c++) {
        SpinLockAcquire(&gRunqLock[c]);
        if (RunqRemoveFromCpuLocked(c, T)) {
            SpinLockRelease(&gRunqLock[c]);
            return;
        }
        SpinLockRelease(&gRunqLock[c]);
    }
    T->InRunq = 0;
}

static UINT32 PickHomeCpu(const TASK *T) {
    int Cpus = HalCpuCount();
    UINT32 Home;
    int Rr;
    if (Cpus < 1) {
        Cpus = 1;
    }
    if (Cpus > HAL_MAX_CPUS) {
        Cpus = HAL_MAX_CPUS;
    }
    if (T && T->Affinity >= 0 && T->Affinity < Cpus) {
        return (UINT32)T->Affinity;
    }
    Rr = __sync_fetch_and_add(&gRoundRobinHome, 1);
    if (Rr < 0) {
        Rr = -Rr;
    }
    Home = (UINT32)(Rr % Cpus);
    return Home;
}

static INT32 TaskSlot(const TASK *T) {
    if (!T) {
        return -1;
    }
    return (INT32)(T - gTasks);
}

/* FD/管道/套接字：见 TaskFd.c（PR-R3） */

void SchedulerInit(void) {
    int c;

    SpinLockInit(&gSchedulerLock);
    gSchedulerOnline = 0;
    gRoundRobinHome = 0;
    gStealCount = 0;
    RunqInit();
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
        gTasks[i].Affinity = -1;
        gTasks[i].OnCpu = -1;
        gTasks[i].HomeCpu = 0;
        gTasks[i].Priority = SCHED_PRIORITY_DEFAULT;
        gTasks[i].InRunq = 0;
        gTasks[i].BrkBase = 0;
        gTasks[i].Brk = 0;
        gTasks[i].MmapNext = 0;
        TaskClearFds(&gTasks[i]);
    }
    gTaskCount = 0;
}

static void CopyName(TASK *T, const char *Name) {
    int n = 0;
    while (Name[n] && n < 15) {
        T->Name[n] = Name[n];
        n++;
    }
    T->Name[n] = 0;
}

static void IdleTask(void) {
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
        gTasks[i].Affinity = -1;
        gTasks[i].OnCpu = -1;
        gTasks[i].HomeCpu = 0;
        gTasks[i].Priority = SCHED_PRIORITY_DEFAULT;
        gTasks[i].InRunq = 0;
        TaskClearFds(&gTasks[i]);
        CopyName(&gTasks[i], Name);
        gTaskCount++;
        {
            UINT32 Home = PickHomeCpu(&gTasks[i]);
            RunqEnqueue(Home, &gTasks[i]);
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
        gTasks[i].Affinity = 0; /* Console/串口非 SMP 安全；用户先钉 BSP */
        gTasks[i].OnCpu = -1;
        gTasks[i].HomeCpu = 0;
        /* 继承创建者优先级，避免 shell/gui(prio=8) 在 UP 上饿死用户(0) */
        gTasks[i].Priority = (Cur && !IsIdleTask(Cur)) ? Cur->Priority : SCHED_PRIORITY_DEFAULT;
        gTasks[i].InRunq = 0;
        gTasks[i].BrkBase = BrkBase;
        gTasks[i].Brk = BrkBase;
        gTasks[i].MmapNext = USER_MMAP_BASE;
        TaskClearFds(&gTasks[i]);
        CopyName(&gTasks[i], Name);
        gTaskCount++;
        {
            UINT32 Home = PickHomeCpu(&gTasks[i]);
            RunqEnqueue(Home, &gTasks[i]);
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
    if (T->State == TASK_READY && T->InRunq) {
        UINT32 Home = (T->HomeCpu >= 0) ? (UINT32)T->HomeCpu : PickHomeCpu(T);
        RunqRemove(T);
        RunqEnqueue(Home, T);
    }
    SpinLockRelease(&gSchedulerLock);
    return 0;
}

static int TaskFitsCpu(const TASK *T, UINT32 Cpu) {
    if (!T || T->State != TASK_READY) {
        return 0;
    }
    if (IsIdleTask(T)) {
        return 0;
    }
    if (T->Affinity >= 0 && (UINT32)T->Affinity != Cpu) {
        return 0;
    }
    return 1;
}

static TASK *PickNext(UINT32 Cpu) {
    TASK *Idle = (Cpu < HAL_MAX_CPUS) ? gIdleTask[Cpu] : 0;
    TASK *T;
    int Cpus;
    int v;
    UINT32 Home;

    /* 1) 本核队列（只持本核 runq 锁） */
    for (;;) {
        SpinLockAcquire(&gRunqLock[Cpu]);
        T = RunqDequeueLocked(Cpu);
        SpinLockRelease(&gRunqLock[Cpu]);
        if (!T) {
            break;
        }
        if (TaskFitsCpu(T, Cpu)) {
            return T;
        }
        Home = PickHomeCpu(T);
        SpinLockAcquire(&gRunqLock[Home]);
        RunqEnqueueLocked(Home, T);
        SpinLockRelease(&gRunqLock[Home]);
    }

    /* 2) 从其它核偷（一次只持一把 victim 锁） */
    Cpus = HalCpuCount();
    if (Cpus < 1) {
        Cpus = 1;
    }
    if (Cpus > HAL_MAX_CPUS) {
        Cpus = HAL_MAX_CPUS;
    }
    for (v = 1; v < Cpus; v++) {
        UINT32 Vic = (Cpu + (UINT32)v) % (UINT32)Cpus;
        SpinLockAcquire(&gRunqLock[Vic]);
        T = RunqStealOneLocked(Vic);
        SpinLockRelease(&gRunqLock[Vic]);
        if (!T) {
            continue;
        }
        if (TaskFitsCpu(T, Cpu)) {
            __sync_fetch_and_add(&gStealCount, 1);
            return T;
        }
        Home = PickHomeCpu(T);
        SpinLockAcquire(&gRunqLock[Home]);
        RunqEnqueueLocked(Home, T);
        SpinLockRelease(&gRunqLock[Home]);
    }

    if (Idle && Idle->State != TASK_UNUSED) {
        if (Idle->State == TASK_RUNNING || Idle->State == TASK_READY) {
            return Idle;
        }
        Idle->State = TASK_READY;
        return Idle;
    }
    return CurrentTask();
}

static UINT64 SchedResumeFrame(TASK *T) {
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
static void ActivateTask(TASK *T) {
    UINT32 Cpu = HalGetCpuId();
    TASK *Prev = CurrentTask();

    if (Prev && Prev != T && Prev->State == TASK_RUNNING) {
        Prev->State = TASK_READY;
        Prev->OnCpu = -1;
        if (!IsIdleTask(Prev)) {
            RunqEnqueue(Cpu, Prev); /* 留在本核队列，利于缓存 */
        }
    }
    RunqRemove(T);
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

static TASK *FindRunnable(UINT32 Cpu) {
    return PickNext(Cpu);
}

static void ReapZombie(TASK *Z) {
    RunqRemove(Z);
    SchedulerFdCloseAll(Z);
    if (Z->UserSpace) {
        VirtualMemorySpaceDestroy(Z->UserSpace);
        Z->UserSpace = 0;
    }
    Z->State = TASK_UNUSED;
    Z->Frame = 0;
    Z->PageRoot = 0;
    Z->IsUser = 0;
    Z->Started = 0;
    Z->ParentId = -1;
    Z->Waiting = 0;
    Z->PendingKill = 0;
    Z->OnCpu = -1;
    Z->InRunq = 0;
    gTaskCount--;
}

/* 仅用户父进程会 wait()；shell/内核为父时直接回收，避免僵尸占满任务槽 */
static int ParentIsUserWaiter(INT32 ParentSlot) {
    TASK *P;

    if (ParentSlot < 0 || ParentSlot >= MAX_TASKS) {
        return 0;
    }
    P = &gTasks[ParentSlot];
    if (P->State == TASK_UNUSED) {
        return 0;
    }
    return P->IsUser;
}

void SchedulerReapOrphanZombies(void) {
    int i;

    SpinLockAcquire(&gSchedulerLock);
    for (i = 0; i < MAX_TASKS; i++) {
        if (gTasks[i].State != TASK_ZOMBIE || !gTasks[i].IsUser) {
            continue;
        }
        if (!ParentIsUserWaiter(gTasks[i].ParentId)) {
            ReapZombie(&gTasks[i]);
        }
    }
    SpinLockRelease(&gSchedulerLock);
}

/* 若父进程正阻塞在 wait：把僵尸结果写入其 Frame 并唤醒，返回 1 表示已收尸 */
static int WakeWaitingParent(TASK *Zombie) {
    TASK *P;
    INT32 ParentId;

    if (!Zombie) {
        return 0;
    }
    ParentId = Zombie->ParentId;
    if (ParentId < 0 || ParentId >= MAX_TASKS) {
        return 0;
    }
    P = &gTasks[ParentId];
    if (P->State != TASK_BLOCKED || !P->Waiting) {
        return 0;
    }
    if (P->Frame) {
        HalFrameSetReturn2(P->Frame,
                           (UINT64)(UINT32)TaskSlot(Zombie),
                           (UINT64)(UINT32)Zombie->ExitCode);
    }
    P->Waiting = 0;
    P->State = TASK_READY;
    RunqEnqueue(PickHomeCpu(P), P);
    ReapZombie(Zombie);
    return 1;
}

/*
 * 持锁：结束用户任务（exit / kill 共用）。
 * *ShowPrompt：无用户父、立即回收时置 1。
 * *OutSpace：调用方在松锁后 VirtualMemorySpaceDestroy（减弱大锁；COW fork 同模式）。
 * 返回 1：目标是当前任务，调用方须切走；0：目标非当前。
 */
static int TerminateUserLocked(TASK *Exiting, INT32 Code, int *ShowPrompt,
                               VIRTUAL_ADDRESS_SPACE **OutSpace) {
    if (OutSpace) {
        *OutSpace = 0;
    }
    if (!Exiting || !Exiting->IsUser) {
        return 0;
    }
    if (ShowPrompt) {
        *ShowPrompt = 0;
    }

    SchedulerFdCloseAll(Exiting);
    if (OutSpace) {
        *OutSpace = Exiting->UserSpace;
    } else if (Exiting->UserSpace) {
        VirtualMemorySpaceDestroy(Exiting->UserSpace);
    }
    Exiting->UserSpace = 0;
    Exiting->ExitCode = Code;
    Exiting->PageRoot = VirtualMemoryKernelRoot();
    Exiting->Waiting = 0;
    Exiting->PendingKill = 0;
    Exiting->OnCpu = -1;
    RunqRemove(Exiting);

    if (ParentIsUserWaiter(Exiting->ParentId)) {
        Exiting->State = TASK_ZOMBIE;
        if (!WakeWaitingParent(Exiting)) {
            /* 父用户进程稍后 wait */
        }
    } else {
        if (ShowPrompt) {
            *ShowPrompt = 1;
        }
        Exiting->State = TASK_UNUSED;
        Exiting->Frame = 0;
        Exiting->ParentId = -1;
        gTaskCount--;
    }

    return Exiting == CurrentTask() ? 1 : 0;
}

static void SchedDestroyDetached(VIRTUAL_ADDRESS_SPACE *Space) {
    if (Space) {
        VirtualMemorySpaceDestroy(Space);
    }
}

static int SignalDefaultTerminates(INT32 Sig) {
    return Sig == SIGKILL || Sig == SIGTERM || Sig == SIGINT;
}

/* 持锁：对用户任务投递默认终止。返回：0 成功且勿切；1 成功且须切走；-1 失败 */
static int DeliverKillLocked(TASK *T, INT32 Sig, int *ShowPrompt,
                             VIRTUAL_ADDRESS_SPACE **OutSpace) {
    INT32 Code;
    UINT32 CurCpu;

    if (!T || !T->IsUser || !SignalDefaultTerminates(Sig)) {
        return -1;
    }
    if (T->State == TASK_UNUSED || T->State == TASK_ZOMBIE) {
        return -1;
    }

    Code = 128 + Sig;
    CurCpu = HalGetCpuId();

    /* 他核 RUNNING：挂起，待该核 timer/syscall 入口完成终止（避免拆用户页表竞态） */
    if (T->State == TASK_RUNNING && T->OnCpu >= 0 &&
        (UINT32)T->OnCpu != CurCpu && T != CurrentTask()) {
        T->PendingKill = Sig;
        return 0;
    }

    return TerminateUserLocked(T, Code, ShowPrompt, OutSpace);
}

UINT64 SchedulerOnTimer(HAL_INTERRUPT_FRAME *Frame) {
    TASK *Cur;
    TASK *Next;
    UINT32 Cpu;
    UINT64 Ret;
    int ShowPrompt = 0;
    VIRTUAL_ADDRESS_SPACE *Detached = 0;

    if (!gSchedulerOnline) {
        return 0;
    }
    Cpu = HalGetCpuId();
    Cur = CurrentTask();
    if (Cur == 0) {
        return 0;
    }
    Cur->Frame = Frame;
    Cur->Ticks++;

    /* 跨核 PendingKill：终止路径仍走任务大锁（锁序：大锁 → runq） */
    if (Cur->IsUser && Cur->PendingKill > 0) {
        INT32 Sig = Cur->PendingKill;
        Cur->PendingKill = 0;
        SpinLockAcquire(&gSchedulerLock);
        if (TerminateUserLocked(Cur, 128 + Sig, &ShowPrompt, &Detached)) {
            Next = FindRunnable(Cpu);
            if (!Next) {
                SpinLockRelease(&gSchedulerLock);
                SchedDestroyDetached(Detached);
                ConsoleWrite("sched: no runnable after pending kill\n");
                for (;;) {
                    HalCpuPark();
                }
            }
            ActivateTask(Next);
            Ret = SchedResumeFrame(Next);
            SpinLockRelease(&gSchedulerLock);
            SchedDestroyDetached(Detached);
            if (ShowPrompt) {
                ConsoleShowPrompt();
            }
            return Ret;
        }
        SpinLockRelease(&gSchedulerLock);
        SchedDestroyDetached(Detached);
    }

    /* PR-S-runq：普通抢占只持每核 runq 锁，两核可并行 PickNext */
    Next = PickNext(Cpu);
    if (Next == Cur || Next == 0) {
        return 0;
    }
    ActivateTask(Next);
    return SchedResumeFrame(Next);
}

static int gCoopDrain;

void SchedulerCoopDrainUsers(void) {
    TASK *Host;
    UINT32 Cpu;
    int i;

    if (!HalPlatformIsVirtSerialConsole()) {
        return;
    }

    Host = CurrentTask();
    if (!Host || Host->IsUser) {
        return;
    }

    gCoopDrain = 1;
    Cpu = HalGetCpuId();

    for (;;) {
        TASK *U = 0;
        UINT64 Ksp;

        SpinLockAcquire(&gSchedulerLock);
        for (i = 0; i < MAX_TASKS; i++) {
            if (gTasks[i].State == TASK_READY && gTasks[i].IsUser &&
                gTasks[i].Frame != 0) {
                U = &gTasks[i];
                break;
            }
        }
        if (!U) {
            SpinLockRelease(&gSchedulerLock);
            break;
        }
        ActivateTask(U);
        U->Started = 1;
        Ksp = (UINT64)(UINTN)(U->Stack + sizeof(U->Stack));
        SpinLockRelease(&gSchedulerLock);

        HalSetKernelStack(Ksp);
        HalUserCoopEnter(Ksp, U->Frame);

        /* exit → HalUserCoopReturn；恢复宿主内核任务 */
        SpinLockAcquire(&gSchedulerLock);
        ActivateTask(Host);
        Host->State = TASK_RUNNING;
        Host->OnCpu = (INT32)Cpu;
        SpinLockRelease(&gSchedulerLock);
        VirtualMemoryLoadPageTable(VirtualMemoryKernelRoot());
    }

    gCoopDrain = 0;
}

UINT64 SchedulerExitUser(HAL_INTERRUPT_FRAME *Frame) {
    INT32 Code;
    TASK *Exiting;
    TASK *Next;
    UINT32 Cpu;
    UINT64 Ret;
    int ShowPrompt = 0;
    VIRTUAL_ADDRESS_SPACE *Detached = 0;

    SpinLockAcquire(&gSchedulerLock);
    Exiting = CurrentTask();
    if (Exiting == 0 || !Exiting->IsUser) {
        SpinLockRelease(&gSchedulerLock);
        for (;;) {
            HalCpuPark();
        }
    }

    Code = (INT32)HalFrameGetArgument0(Frame);
    DebugWrite("syscall: exit ");
    DebugWrite(Exiting->Name);
    DebugWrite(" code=");
    DebugHex32((UINT32)Code);
    DebugWrite("\n");

    (void)TerminateUserLocked(Exiting, Code, &ShowPrompt, &Detached);

    if (gCoopDrain) {
        SpinLockRelease(&gSchedulerLock);
        SchedDestroyDetached(Detached);
        if (ShowPrompt) {
            ConsoleShowPrompt();
        }
        VirtualMemoryLoadPageTable(VirtualMemoryKernelRoot());
        HalUserCoopReturn();
        return 0;
    }

    Cpu = HalGetCpuId();
    Next = FindRunnable(Cpu);
    if (!Next) {
        SpinLockRelease(&gSchedulerLock);
        SchedDestroyDetached(Detached);
        ConsoleWrite("sched: no runnable task after exit\n");
        for (;;) {
            HalCpuPark();
        }
    }
    ActivateTask(Next);
    Ret = SchedResumeFrame(Next);
    SpinLockRelease(&gSchedulerLock);
    SchedDestroyDetached(Detached);
    if (ShowPrompt) {
        ConsoleShowPrompt();
    }
    return Ret;
}

UINT64 SchedulerFork(HAL_INTERRUPT_FRAME *Frame) {
    VIRTUAL_ADDRESS_SPACE *ChildSpace;
    TASK *Parent;
    INT32 ParentSlot;
    int Child;
    UINT8 *Top;
    HAL_INTERRUPT_FRAME *CF;

    SpinLockAcquire(&gSchedulerLock);
    Parent = CurrentTask();
    ParentSlot = TaskSlot(Parent);
    if (Parent == 0 || ParentSlot < 0 || !Parent->IsUser || !Parent->UserSpace) {
        HalFrameSetReturn(Frame, (UINT64)(INT64)-1);
        SpinLockRelease(&gSchedulerLock);
        return 0;
    }

    Child = -1;
    for (int i = 0; i < MAX_TASKS; i++) {
        if (gTasks[i].State == TASK_UNUSED) {
            Child = i;
            break;
        }
    }
    if (Child < 0) {
        HalFrameSetReturn(Frame, (UINT64)(INT64)-1);
        SpinLockRelease(&gSchedulerLock);
        return 0;
    }

    HalFrameSetReturn(Frame, (UINT64)(UINT32)(Child + 1));
    /*
     * 先切内核 CR3 再 COW 克隆：父 UserSpace 正是当前 CR3 时改 PTE 会踩 TLB。
     * 克隆期间保持关中断（本路径自 SyscallDispatch 起 IF=0；松锁不恢复 IF）。
     */
    VirtualMemoryLoadPageTable(VirtualMemoryKernelRoot());
    SpinLockRelease(&gSchedulerLock);

    ChildSpace = VirtualMemorySpaceClone(Parent->UserSpace);
    if (!ChildSpace) {
        VirtualMemoryLoadPageTable(Parent->PageRoot);
        HalFrameSetReturn(Frame, (UINT64)(INT64)-1);
        return 0;
    }

    SpinLockAcquire(&gSchedulerLock);
    /* 槽位仍应空闲；若竞态被占则放弃 */
    if (gTasks[Child].State != TASK_UNUSED) {
        SpinLockRelease(&gSchedulerLock);
        VirtualMemorySpaceDestroy(ChildSpace);
        VirtualMemoryLoadPageTable(Parent->PageRoot);
        HalFrameSetReturn(Frame, (UINT64)(INT64)-1);
        return 0;
    }

    Top = gTasks[Child].Stack + sizeof(gTasks[Child].Stack);
    CF = (HAL_INTERRUPT_FRAME *)(Top - sizeof(HAL_INTERRUPT_FRAME));
    HalFrameCopy(CF, Frame);
    HalFrameSetReturn(CF, 0);

    gTasks[Child].Frame = CF;
    gTasks[Child].State = TASK_READY;
    gTasks[Child].Ticks = 0;
    gTasks[Child].PageRoot = VirtualMemorySpaceRoot(ChildSpace);
    gTasks[Child].IsUser = 1;
    /* 帧从父 syscall 拷来：首次调度须走普通恢复，禁止 USER_FIRST/UserEnter */
    gTasks[Child].Started = 1;
    gTasks[Child].UserSpace = ChildSpace;
    gTasks[Child].ParentId = ParentSlot;
    gTasks[Child].ExitCode = 0;
    gTasks[Child].Waiting = 0;
    gTasks[Child].PendingKill = 0;
    gTasks[Child].Affinity = 0; /* 与 CreateUser 一致：Console 钉 BSP */
    gTasks[Child].OnCpu = -1;
    gTasks[Child].HomeCpu = 0;
    gTasks[Child].Priority = Parent->Priority;
    gTasks[Child].InRunq = 0;
    gTasks[Child].BrkBase = Parent->BrkBase;
    gTasks[Child].Brk = Parent->Brk;
    gTasks[Child].MmapNext = Parent->MmapNext;
    TaskCloneFds(&gTasks[Child], Parent);
    CopyName(&gTasks[Child], Parent->Name);
    gTaskCount++;
    RunqEnqueue(PickHomeCpu(&gTasks[Child]), &gTasks[Child]);

    HalFrameSetReturn(Frame, (UINT64)(UINT32)(Child + 1));
    Parent->Frame = Frame;
    VirtualMemoryLoadPageTable(Parent->PageRoot);
    SpinLockRelease(&gSchedulerLock);
    return 0;
}

UINT64 SchedulerWait(HAL_INTERRUPT_FRAME *Frame) {
    TASK *Self;
    INT32 MyId;
    int i;
    int Live = 0;
    TASK *Next;
    UINT64 Options;
    UINT32 Cpu;
    UINT64 Ret;

    SpinLockAcquire(&gSchedulerLock);
    Self = CurrentTask();
    if (Self == 0 || !Self->IsUser) {
        HalFrameSetReturn(Frame, (UINT64)(INT64)-1);
        SpinLockRelease(&gSchedulerLock);
        return 0;
    }
    MyId = TaskSlot(Self);
    Options = HalFrameGetArgument0(Frame);

    for (i = 0; i < MAX_TASKS; i++) {
        if (gTasks[i].State == TASK_ZOMBIE && gTasks[i].ParentId == MyId) {
            INT32 Code = gTasks[i].ExitCode;
            INT32 Cid = TaskSlot(&gTasks[i]);
            ReapZombie(&gTasks[i]);
            HalFrameSetReturn2(Frame, (UINT64)(UINT32)Cid, (UINT64)(UINT32)Code);
            SpinLockRelease(&gSchedulerLock);
            return 0;
        }
    }
    for (i = 0; i < MAX_TASKS; i++) {
        if (gTasks[i].ParentId == MyId &&
            (gTasks[i].State == TASK_READY ||
             gTasks[i].State == TASK_RUNNING ||
             gTasks[i].State == TASK_BLOCKED)) {
            Live = 1;
            break;
        }
    }
    if (!Live) {
        HalFrameSetReturn(Frame, (UINT64)(INT64)-1);
        SpinLockRelease(&gSchedulerLock);
        return 0;
    }

    if (Options & (UINT64)WNOHANG) {
        HalFrameSetReturn2(Frame, 0, 0);
        SpinLockRelease(&gSchedulerLock);
        return 0;
    }

    Self->Frame = Frame;
    Self->Waiting = 1;
    Self->State = TASK_BLOCKED;
    Self->OnCpu = -1;

    Cpu = HalGetCpuId();
    Next = FindRunnable(Cpu);
    if (!Next) {
        SpinLockRelease(&gSchedulerLock);
        ConsoleWrite("sched: wait with no runnable task\n");
        for (;;) {
            HalCpuPark();
        }
    }
    ActivateTask(Next);
    Ret = SchedResumeFrame(Next);
    SpinLockRelease(&gSchedulerLock);
    return Ret;
}

UINT64 SchedulerKill(HAL_INTERRUPT_FRAME *Frame) {
    INT32 Pid;
    INT32 Sig;
    INT32 Slot;
    TASK *T;
    TASK *Next;
    UINT32 Cpu;
    UINT64 Ret;
    int ShowPrompt = 0;
    int Deliver;
    VIRTUAL_ADDRESS_SPACE *Detached = 0;

    SpinLockAcquire(&gSchedulerLock);
    Pid = (INT32)HalFrameGetArgument0(Frame);
    Sig = (INT32)HalFrameGetArgument1(Frame);
    if (Pid <= 0 || !SignalDefaultTerminates(Sig)) {
        HalFrameSetReturn(Frame, (UINT64)(INT64)-1);
        SpinLockRelease(&gSchedulerLock);
        return 0;
    }
    Slot = Pid - 1;
    if (Slot < 0 || Slot >= MAX_TASKS) {
        HalFrameSetReturn(Frame, (UINT64)(INT64)-1);
        SpinLockRelease(&gSchedulerLock);
        return 0;
    }
    T = &gTasks[Slot];
    Deliver = DeliverKillLocked(T, Sig, &ShowPrompt, &Detached);
    if (Deliver < 0) {
        HalFrameSetReturn(Frame, (UINT64)(INT64)-1);
        SpinLockRelease(&gSchedulerLock);
        return 0;
    }
    if (Deliver == 0) {
        HalFrameSetReturn(Frame, 0);
        SpinLockRelease(&gSchedulerLock);
        SchedDestroyDetached(Detached);
        return 0;
    }

    /* 杀自身：切到其他可运行任务 */
    Cpu = HalGetCpuId();
    Next = FindRunnable(Cpu);
    if (!Next) {
        SpinLockRelease(&gSchedulerLock);
        SchedDestroyDetached(Detached);
        ConsoleWrite("sched: no runnable after self-kill\n");
        for (;;) {
            HalCpuPark();
        }
    }
    ActivateTask(Next);
    Ret = SchedResumeFrame(Next);
    SpinLockRelease(&gSchedulerLock);
    SchedDestroyDetached(Detached);
    if (ShowPrompt) {
        ConsoleShowPrompt();
    }
    return Ret;
}

int SchedulerKillPid(INT32 Pid, INT32 Sig) {
    INT32 Slot;
    TASK *T;
    int ShowPrompt = 0;
    int Deliver;
    TASK *Cur;
    TASK *Next;
    UINT32 Cpu;
    UINT64 Ret;
    VIRTUAL_ADDRESS_SPACE *Detached = 0;

    if (Pid <= 0 || !SignalDefaultTerminates(Sig)) {
        return -1;
    }
    Slot = Pid - 1;
    if (Slot < 0 || Slot >= MAX_TASKS) {
        return -1;
    }

    SpinLockAcquire(&gSchedulerLock);
    T = &gTasks[Slot];
    Cur = CurrentTask();
    Deliver = DeliverKillLocked(T, Sig, &ShowPrompt, &Detached);
    if (Deliver < 0) {
        SpinLockRelease(&gSchedulerLock);
        return -1;
    }
    if (Deliver == 0 || T != Cur) {
        SpinLockRelease(&gSchedulerLock);
        SchedDestroyDetached(Detached);
        if (ShowPrompt) {
            ConsoleShowPrompt();
        }
        return 0;
    }

    Cpu = HalGetCpuId();
    Next = FindRunnable(Cpu);
    if (!Next) {
        SpinLockRelease(&gSchedulerLock);
        SchedDestroyDetached(Detached);
        return -1;
    }
    ActivateTask(Next);
    Ret = SchedResumeFrame(Next);
    (void)Ret;
    SpinLockRelease(&gSchedulerLock);
    SchedDestroyDetached(Detached);
    if (ShowPrompt) {
        ConsoleShowPrompt();
    }
    return 0;
}

UINT64 SchedulerYield(HAL_INTERRUPT_FRAME *Frame) {
    TASK *Cur;
    TASK *Next;
    UINT32 Cpu;

    Cur = CurrentTask();
    if (Cur == 0) {
        return 0;
    }
    Cur->Frame = Frame;
    Cpu = HalGetCpuId();
    /* PR-S-runq：yield 与 timer 同形，不持任务大锁 */
    Next = PickNext(Cpu);
    if (Next == Cur || Next == 0) {
        return 0;
    }
    ActivateTask(Next);
    return SchedResumeFrame(Next);
}

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
    gIdleTask[Cpu] = &gTasks[Id];
    gIdleTask[Cpu]->Priority = SCHED_PRIORITY_IDLE;
    RunqRemove(gIdleTask[Cpu]);
    SpinLockRelease(&gSchedulerLock);
    return Id;
}

int SchedulerIsOnline(void) {
    return gSchedulerOnline;
}

void SchedulerApStart(void) {
    UINT32 Cpu;
    TASK *Idle;
    UINT64 Ret;

    Cpu = HalGetCpuId();
    while (!gSchedulerOnline) {
        HalCpuRelax();
    }
    SpinLockAcquire(&gSchedulerLock);
    Idle = (Cpu < HAL_MAX_CPUS) ? gIdleTask[Cpu] : 0;
    if (!Idle) {
        SpinLockRelease(&gSchedulerLock);
        HalDebugWrite("sched: AP has no idle\n");
        for (;;) {
            HalCpuPark();
        }
    }
    ActivateTask(Idle);
    Idle->Started = 1;
    Ret = (UINT64)(UINTN)Idle->Frame;
    SpinLockRelease(&gSchedulerLock);
    HalDebugWrite("sched: AP entered idle cpu=");
    HalDebugWriteHex32(Cpu);
    HalDebugWrite("\n");
    HalSchedulerEnter(Idle->Frame);
    (void)Ret;
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
     * PR-S-ap：多核时 shell/gui 同钉 AP（逻辑 CPU1），BSP 留给 idle0 / 中断 / 偷任务；
     * 单核仍钉 0。交互 Priority 偏高；worker Affinity=-1。
     */
    {
        UINT32 InteractiveCpu = (Cpus > 1) ? 1u : 0u;

        for (i = 0; i < MAX_TASKS; i++) {
            if (gTasks[i].State == TASK_UNUSED) {
                continue;
            }
            if ((gTasks[i].Name[0] == 's' && gTasks[i].Name[1] == 'h') ||
                (gTasks[i].Name[0] == 'g' && gTasks[i].Name[1] == 'u')) {
                RunqRemove(&gTasks[i]);
                gTasks[i].Affinity = (INT32)InteractiveCpu;
                gTasks[i].HomeCpu = (INT32)InteractiveCpu;
                gTasks[i].Priority = SCHED_PRIORITY_SHELL;
                RunqEnqueue(InteractiveCpu, &gTasks[i]);
            }
        }
    }

    First = 0;
    for (i = 0; i < MAX_TASKS; i++) {
        if (gTasks[i].State != TASK_READY) {
            continue;
        }
        if (gIdleTask[0] && &gTasks[i] == gIdleTask[0]) {
            continue;
        }
        /* BSP 勿直接切入钉在 AP 上的任务 */
        if (gTasks[i].Affinity >= 0 && gTasks[i].Affinity != 0) {
            continue;
        }
        First = &gTasks[i];
        break;
    }
    if (!First) {
        First = gIdleTask[0];
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
    SpinLockRelease(&gSchedulerLock);
    HalTimerStart();
    DebugWrite("sched: online, entering tasks\n");
    HalSchedulerEnter(First->Frame);
}

TASK *SchedulerCurrent(void) {
    return CurrentTask();
}

int SchedulerTaskCount(void) {
    return gTaskCount;
}

UINT64 SchedulerStealCount(void) {
    return gStealCount;
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
