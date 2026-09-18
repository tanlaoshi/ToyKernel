/*
 * Scheduler.c — 抢占式任务调度器（fork/kill/SMP；runq 见 SchedRunq.c；wait/exit 见 SchedWait.c；FD 见 TaskFd.c）
 */
#include "Scheduler.h"
#include "SchedulerPriv.h"
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
static volatile int gSchedulerOnline;
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

UINT64 SchedResumeFrame(TASK *T) {
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

static TASK *FindRunnable(UINT32 Cpu) {
    return PickNext(Cpu);
}

static int SignalDefaultTerminates(INT32 Sig) {
    return Sig == SIGKILL || Sig == SIGTERM || Sig == SIGINT;
}

#define SIG_HANDLER_DFL 0ULL
#define SIG_HANDLER_IGN 1ULL

static UINT64 *SignalHandlerSlot(TASK *T, INT32 Sig) {
    if (!T) {
        return 0;
    }
    if (Sig == SIGINT) {
        return &T->SigHandlerInt;
    }
    if (Sig == SIGTERM) {
        return &T->SigHandlerTerm;
    }
    return 0;
}

static UINT64 SignalHandlerGet(TASK *T, INT32 Sig) {
    UINT64 *Slot = SignalHandlerSlot(T, Sig);
    return Slot ? *Slot : SIG_HANDLER_DFL;
}

/* 把用户帧改成进入 handler；成功 0 */
static int DeliverToHandlerFrame(TASK *T, HAL_INTERRUPT_FRAME *F, UINT64 Handler,
                                 INT32 Sig) {
    UINT64 ResumeIp = 0;
    UINT64 PushSp = 0;
    int NeedPush;
    UINT64 SavedRoot = 0;
    int Switched = 0;

    if (!T || !F || !T->IsUser || Handler < 2) {
        return -1;
    }
    NeedPush = HalFrameSignalSetup(F, Handler, (UINT64)(UINT32)Sig, &ResumeIp, &PushSp);
    if (NeedPush < 0) {
        return -1;
    }
    if (NeedPush == 1) {
        /* 压栈须在目标用户页表下（含 fork COW 拆页） */
        if (T != CurrentTask() && T->PageRoot != 0) {
            SavedRoot = HalGetCurrentPageTable();
            VirtualMemoryLoadPageTable(T->PageRoot);
            Switched = 1;
        }
        if (VirtualMemoryCopyToUser(PushSp, &ResumeIp, sizeof(ResumeIp)) < 0) {
            if (Switched) {
                VirtualMemoryLoadPageTable(SavedRoot);
            }
            return -1;
        }
        if (Switched) {
            VirtualMemoryLoadPageTable(SavedRoot);
        }
        HalFrameSetStackPointer(F, PushSp);
    }
    return 0;
}

/*
 * 持锁：投递信号。返回：0 成功且勿切；1 成功且须切走；-1 失败。
 * LiveFrame：本核当前中断帧（可为 0）；用于立即改写当前用户任务。
 */
static int DeliverKillLocked(TASK *T, INT32 Sig, int *ShowPrompt,
                             VIRTUAL_ADDRESS_SPACE **OutSpace,
                             HAL_INTERRUPT_FRAME *LiveFrame) {
    INT32 Code;
    UINT32 CurCpu;
    UINT64 Handler;

    if (!T || !T->IsUser || !SignalDefaultTerminates(Sig)) {
        return -1;
    }
    if (T->State == TASK_UNUSED || T->State == TASK_ZOMBIE) {
        return -1;
    }

    Handler = SignalHandlerGet(T, Sig);
    if (Sig != SIGKILL && Handler == SIG_HANDLER_IGN) {
        return 0;
    }
    if (Sig != SIGKILL && Handler > SIG_HANDLER_IGN) {
        CurCpu = HalGetCpuId();
        if (T->State == TASK_RUNNING && T->OnCpu >= 0 &&
            (UINT32)T->OnCpu != CurCpu && T != CurrentTask()) {
            T->PendingKill = Sig;
            return 0;
        }
        {
            HAL_INTERRUPT_FRAME *F = (T == CurrentTask() && LiveFrame) ? LiveFrame : T->Frame;
            if (!F || DeliverToHandlerFrame(T, F, Handler, Sig) != 0) {
                return -1;
            }
            if (T == CurrentTask() && LiveFrame) {
                T->Frame = LiveFrame;
            }
        }
        return 0;
    }

    Code = 128 + Sig;
    CurCpu = HalGetCpuId();

    /* 他核 RUNNING：挂起，待该核 timer/syscall 入口完成终止 */
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

    /* 跨核 PendingKill：终止或 handler（锁序：大锁 → runq） */
    if (Cur->IsUser && Cur->PendingKill > 0) {
        INT32 Sig = Cur->PendingKill;
        UINT64 Handler;
        Cur->PendingKill = 0;
        SpinLockAcquire(&gSchedulerLock);
        Handler = SignalHandlerGet(Cur, Sig);
        if (Sig != SIGKILL && Handler == SIG_HANDLER_IGN) {
            SpinLockRelease(&gSchedulerLock);
        } else if (Sig != SIGKILL && Handler > SIG_HANDLER_IGN) {
            if (DeliverToHandlerFrame(Cur, Frame, Handler, Sig) != 0) {
                SpinLockRelease(&gSchedulerLock);
            } else {
                Cur->Frame = Frame;
                SpinLockRelease(&gSchedulerLock);
                return 0;
            }
        } else if (TerminateUserLocked(Cur, 128 + Sig, &ShowPrompt, &Detached)) {
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
        } else {
            SpinLockRelease(&gSchedulerLock);
            SchedDestroyDetached(Detached);
        }
    }

    /* PR-S-runq：普通抢占只持每核 runq 锁，两核可并行 PickNext */
    Next = PickNext(Cpu);
    if (Next == Cur || Next == 0) {
        return 0;
    }
    ActivateTask(Next);
    return SchedResumeFrame(Next);
}

int gCoopDrain;

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
    gTasks[Child].SigHandlerInt = Parent->SigHandlerInt;
    gTasks[Child].SigHandlerTerm = Parent->SigHandlerTerm;
    gTasks[Child].Affinity = 0; /* 与 CreateUser 一致：Console 钉 BSP */
    gTasks[Child].OnCpu = -1;
    gTasks[Child].HomeCpu = 0;
    gTasks[Child].Priority = Parent->Priority;
    gTasks[Child].InRunQueue = 0;
    gTasks[Child].BrkBase = Parent->BrkBase;
    gTasks[Child].Brk = Parent->Brk;
    gTasks[Child].MmapNext = Parent->MmapNext;
    TaskCloneFds(&gTasks[Child], Parent);
    CopyName(&gTasks[Child], Parent->Name);
    gTaskCount++;
    RunQueueEnqueue(PickHomeCpu(&gTasks[Child]), &gTasks[Child]);

    HalFrameSetReturn(Frame, (UINT64)(UINT32)(Child + 1));
    Parent->Frame = Frame;
    VirtualMemoryLoadPageTable(Parent->PageRoot);
    SpinLockRelease(&gSchedulerLock);
    return 0;
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
    Deliver = DeliverKillLocked(T, Sig, &ShowPrompt, &Detached, Frame);
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

UINT64 SchedulerSignal(HAL_INTERRUPT_FRAME *Frame) {
    TASK *T;
    INT32 Sig;
    UINT64 Handler;
    UINT64 *Slot;
    UINT64 Old;

    SpinLockAcquire(&gSchedulerLock);
    T = CurrentTask();
    if (!T || !T->IsUser) {
        HalFrameSetReturn(Frame, (UINT64)(INT64)-1);
        SpinLockRelease(&gSchedulerLock);
        return 0;
    }
    Sig = (INT32)HalFrameGetArgument0(Frame);
    Handler = HalFrameGetArgument1(Frame);
    if (Sig == SIGKILL) {
        if (Handler != SIG_HANDLER_DFL) {
            HalFrameSetReturn(Frame, (UINT64)(INT64)-1);
            SpinLockRelease(&gSchedulerLock);
            return 0;
        }
        HalFrameSetReturn(Frame, SIG_HANDLER_DFL);
        SpinLockRelease(&gSchedulerLock);
        return 0;
    }
    Slot = SignalHandlerSlot(T, Sig);
    if (!Slot) {
        HalFrameSetReturn(Frame, (UINT64)(INT64)-1);
        SpinLockRelease(&gSchedulerLock);
        return 0;
    }
    Old = *Slot;
    *Slot = Handler;
    HalFrameSetReturn(Frame, Old);
    SpinLockRelease(&gSchedulerLock);
    return 0;
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
    Deliver = DeliverKillLocked(T, Sig, &ShowPrompt, &Detached,
                                (T == Cur && Cur) ? Cur->Frame : 0);
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
    RunQueueRemove(gIdleTask[Cpu]);
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
        ToyLogSmp("sched: AP has no idle\n");
        for (;;) {
            HalCpuPark();
        }
    }
    ActivateTask(Idle);
    Idle->Started = 1;
    Ret = (UINT64)(UINTN)Idle->Frame;
    SpinLockRelease(&gSchedulerLock);
    /* 8 AP 并发写 COM1 会把欢迎语打成乱码；只留一条样例给冒烟 */
    if (Cpu == 1) {
        ToyLogSmp("sched: AP entered idle cpu=");
        ToyLogSmpHex32(Cpu);
        ToyLogSmp("\n");
    }
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
     * PR-S-input-pin 序 2：input 钉独立 CPU2（SMP≥3），与 shell/gui 分核，sti 不外溢。
     */
    {
        UINT32 InteractiveCpu = (Cpus > 1) ? 1u : 0u;
        UINT32 InputCpu = (Cpus > 2) ? 2u : InteractiveCpu;

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
