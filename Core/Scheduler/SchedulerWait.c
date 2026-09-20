/*
 * SchedulerWait.c — PR-S-sched-split-2：wait / zombie / reap / exit 收尸
 *
 * 从 Scheduler.c 原样搬家；不改语义。Kill/timer 经 TerminateUserLocked /
 * SchedulerDestroyDetached（见 SchedulerPrivate.h）。
 */
#include "SchedulerPrivate.h"
#include "TaskFd.h"
#include "Syscall.h"
#include "Hal.h"
#include "Console.h"
#include "Gui.h"
#include "Debug.h"
#include "VirtualMemory.h"

static void ReapZombie(TASK *Z) {
    RunQueueRemove(Z);
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
    Z->InRunQueue = 0;
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
    RunQueueEnqueue(PickHomeCpu(P), P);
    ReapZombie(Zombie);
    return 1;
}

/*
 * 持锁：结束用户任务（exit / kill 共用）。
 * *ShowPrompt：无用户父、立即回收时置 1。
 * *OutSpace：调用方在松锁后 VirtualMemorySpaceDestroy（减弱大锁；COW fork 同模式）。
 * 返回 1：目标是当前任务，调用方须切走；0：目标非当前。
 */
int TerminateUserLocked(TASK *Exiting, INT32 Code, int *ShowPrompt,
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
    RunQueueRemove(Exiting);

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

void SchedulerDestroyDetached(VIRTUAL_ADDRESS_SPACE *Space) {
    if (Space) {
        VirtualMemorySpaceDestroy(Space);
    }
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

    /*
     * 先收 USER 窗再切任务：避免与点击关窗淡出重入；也不要在
     * ActivateTask 之后做重 GUI（当时 Current 已是 Shell）。
     */
    SpinLockRelease(&gSchedulerLock);
    GuiCloseAllUserWindows();
    SchedulerDestroyDetached(Detached);
    Detached = 0;
    SpinLockAcquire(&gSchedulerLock);

    if (gCoopDrain) {
        SpinLockRelease(&gSchedulerLock);
        if (ShowPrompt) {
            ConsoleShowPrompt();
        }
        VirtualMemoryLoadPageTable(VirtualMemoryKernelRoot());
        HalUserCoopReturn();
        return 0;
    }

    Cpu = HalGetCpuId();
    Next = PickNext(Cpu);
    if (!Next) {
        SpinLockRelease(&gSchedulerLock);
        ConsoleWrite("sched: no runnable task after exit\n");
        for (;;) {
            HalCpuPark();
        }
    }
    ActivateTask(Next);
    Ret = SchedulerResumeFrame(Next);
    SpinLockRelease(&gSchedulerLock);
    if (ShowPrompt) {
        ConsoleShowPrompt();
    }
    return Ret;
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
    Next = PickNext(Cpu);
    if (!Next) {
        SpinLockRelease(&gSchedulerLock);
        ConsoleWrite("sched: wait with no runnable task\n");
        for (;;) {
            HalCpuPark();
        }
    }
    ActivateTask(Next);
    Ret = SchedulerResumeFrame(Next);
    SpinLockRelease(&gSchedulerLock);
    return Ret;
}
