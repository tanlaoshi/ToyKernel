/*
 * SchedulerThreadSys.c — PR-U-thread-3：THREAD_CREATE / JOIN / EXIT
 */
#include "Scheduler.h"
#include "SchedulerOps.h"
#include "SchedulerPrivate.h"
#include "Hal.h"
#include "VirtualMemory.h"
#include "SpinLock.h"
#include "Errno.h"

UINT64 SchedulerThreadCreate(HAL_INTERRUPT_FRAME *Frame) {
    TASK *Self;
    UINT64 Entry;
    UINT64 Arg0;
    UINT64 Arg1;
    int Slot;

    SpinLockAcquire(&gSchedulerLock);
    Self = CurrentTask();
    if (!Self || !Self->IsUser || !Self->UserSpace) {
        HalFrameSetReturn(Frame, (UINT64)(INT64)(-(INT64)TOY_EINVAL));
        SpinLockRelease(&gSchedulerLock);
        return 0;
    }
    Entry = HalFrameGetArgument0(Frame);
    Arg0 = HalFrameGetArgument1(Frame);
    Arg1 = HalFrameGetArgument2(Frame);
    SpinLockRelease(&gSchedulerLock);

    if (Entry == 0) {
        HalFrameSetReturn(Frame, (UINT64)(INT64)(-(INT64)TOY_EINVAL));
        return 0;
    }

    Slot = SchedulerCreateThread(Self, "thread", Entry, Arg0, Arg1, 0, 1);
    if (Slot < 0) {
        HalFrameSetReturn(Frame, (UINT64)(INT64)(-(INT64)TOY_EAGAIN));
        return 0;
    }
    HalFrameSetReturn(Frame, (UINT64)gTasks[Slot].Id);
    return 0;
}

UINT64 SchedulerThreadJoin(HAL_INTERRUPT_FRAME *Frame) {
    TASK *Self;
    INT32 Tid;
    UINT64 StatusUser;
    int i;
    TASK *Target;
    TASK *Next;
    UINT32 Cpu;
    UINT64 Ret;
    INT32 Code;

    SpinLockAcquire(&gSchedulerLock);
    Self = CurrentTask();
    if (!Self || !Self->IsUser) {
        HalFrameSetReturn(Frame, (UINT64)(INT64)(-(INT64)TOY_EINVAL));
        SpinLockRelease(&gSchedulerLock);
        return 0;
    }
    Tid = (INT32)HalFrameGetArgument0(Frame);
    StatusUser = HalFrameGetArgument1(Frame);
    if (Tid < 0 || Tid >= MAX_TASKS) {
        HalFrameSetReturn(Frame, (UINT64)(INT64)(-(INT64)TOY_EINVAL));
        SpinLockRelease(&gSchedulerLock);
        return 0;
    }

    Target = 0;
    for (i = 0; i < MAX_TASKS; i++) {
        if (gTasks[i].State == TASK_UNUSED || !gTasks[i].IsUser) {
            continue;
        }
        if ((INT32)gTasks[i].Id != Tid) {
            continue;
        }
        if (gTasks[i].GroupId != Self->GroupId) {
            HalFrameSetReturn(Frame, (UINT64)(INT64)(-(INT64)TOY_EPERM));
            SpinLockRelease(&gSchedulerLock);
            return 0;
        }
        Target = &gTasks[i];
        break;
    }
    if (!Target) {
        HalFrameSetReturn(Frame, (UINT64)(INT64)(-(INT64)TOY_ESRCH));
        SpinLockRelease(&gSchedulerLock);
        return 0;
    }
    if (Target == Self) {
        HalFrameSetReturn(Frame, (UINT64)(INT64)(-(INT64)TOY_EINVAL));
        SpinLockRelease(&gSchedulerLock);
        return 0;
    }

    if (Target->State == TASK_ZOMBIE) {
        Code = Target->ExitCode;
        SchedulerReapZombie(Target);
        SpinLockRelease(&gSchedulerLock);
        if (StatusUser != 0) {
            (void)VirtualMemoryCopyToUser(StatusUser, &Code, sizeof(Code));
        }
        HalFrameSetReturn2(Frame, 0, (UINT64)(UINT32)Code);
        return 0;
    }

    if (Target->JoinerSlot >= 0) {
        HalFrameSetReturn(Frame, (UINT64)(INT64)(-(INT64)TOY_EINVAL));
        SpinLockRelease(&gSchedulerLock);
        return 0;
    }

    Self->Frame = Frame;
    Self->Waiting = 1;
    Self->JoinTid = Tid;
    Target->JoinerSlot = TaskSlot(Self);
    Self->State = TASK_BLOCKED;
    SchedulerOpsGet()->Remove(Self);

    Cpu = HalCpuGetId();
    Next = SchedulerOpsGet()->PickNext(Cpu);
    if (!Next) {
        SpinLockRelease(&gSchedulerLock);
        for (;;) {
            HalCpuPark();
        }
    }
    ActivateTask(Next);
    Ret = SchedulerResumeFrame(Next);
    SpinLockRelease(&gSchedulerLock);
    (void)StatusUser;
    return Ret;
}

UINT64 SchedulerThreadExit(HAL_INTERRUPT_FRAME *Frame) {
    /* 与 SYS_EXIT 同收尸路径；非末活 → joinable zombie */
    return SchedulerExitUser(Frame);
}
