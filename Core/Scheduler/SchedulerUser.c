/*
 * SchedulerUser.c — fork / kill / yield（PR-S-sched-1）
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
    gTasks[Child].SleepWakeTick = 0;
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
    {
        int k;
        for (k = 0; k < (int)sizeof(Parent->Cwd); k++) {
            gTasks[Child].Cwd[k] = Parent->Cwd[k];
        }
    }
    TaskCloneFds(&gTasks[Child], Parent);
    CopyName(&gTasks[Child], Parent->Name);
    gTaskCount++;
    RunQueueEnqueue(SchedulerOpsGet()->PickHome(&gTasks[Child]), &gTasks[Child]);

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
        SchedulerDestroyDetached(Detached);
        return 0;
    }

    /* 杀自身：切到其他可运行任务 */
    Cpu = HalGetCpuId();
    Next = FindRunnable(Cpu);
    if (!Next) {
        SpinLockRelease(&gSchedulerLock);
        VirtualMemoryLoadPageTable(VirtualMemoryKernelRoot());
        SchedulerDestroyDetached(Detached);
        ConsoleWrite("sched: no runnable after self-kill\n");
        for (;;) {
            HalCpuPark();
        }
    }
    ActivateTask(Next);
    Ret = SchedulerResumeFrame(Next);
    SpinLockRelease(&gSchedulerLock);
    SchedulerDestroyDetached(Detached);
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
        SchedulerDestroyDetached(Detached);
        if (ShowPrompt) {
            ConsoleShowPrompt();
        }
        return 0;
    }

    Cpu = HalGetCpuId();
    Next = FindRunnable(Cpu);
    if (!Next) {
        SpinLockRelease(&gSchedulerLock);
        SchedulerDestroyDetached(Detached);
        return -1;
    }
    ActivateTask(Next);
    Ret = SchedulerResumeFrame(Next);
    (void)Ret;
    SpinLockRelease(&gSchedulerLock);
    SchedulerDestroyDetached(Detached);
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
    Next = SchedulerOpsGet()->PickNext(Cpu);
    if (Next == Cur || Next == 0) {
        return 0;
    }
    ActivateTask(Next);
    return SchedulerResumeFrame(Next);
}
