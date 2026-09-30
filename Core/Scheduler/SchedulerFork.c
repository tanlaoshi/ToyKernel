/*
 * SchedulerFork.c — fork COW 克隆（PR-S3-scheduser-1）
 *
 * 从 SchedulerUser.c 原样搬家；不改语义。
 */
#include "Scheduler.h"
#include "SchedulerOps.h"
#include "SchedulerPrivate.h"
#include "TaskFd.h"
#include "Hal.h"
#include "VirtualMemory.h"
#include "SpinLock.h"
#include "Errno.h"

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
    /* PR-U-thread：组内 >1 线程则拒绝（thr-0 钉死；避免半拷贝） */
    if (SchedulerGroupAliveCount(Parent->GroupId) > 1) {
        HalFrameSetReturn(Frame, (UINT64)(INT64)(-(INT64)TOY_EAGAIN));
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
    gTasks[Child].GroupId = (INT32)gTasks[Child].Id; /* 新进程：自为组主 */
    gTasks[Child].LeaderId = (INT32)gTasks[Child].Id;
    gTasks[Child].IsThread = 0;
    gTasks[Child].TlsBase = 0;
    gTasks[Child].JoinerSlot = -1;
    gTasks[Child].JoinTid = -1;
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
    /* 先不入队；松锁 EnsureTls 后再挂 */
    HalFrameSetReturn(Frame, (UINT64)(UINT32)(Child + 1));
    Parent->Frame = Frame;
    VirtualMemoryLoadPageTable(Parent->PageRoot);
    SpinLockRelease(&gSchedulerLock);
    (void)SchedulerThreadEnsureTls(&gTasks[Child]);
    SpinLockAcquire(&gSchedulerLock);
    if (gTasks[Child].State == TASK_READY && !gTasks[Child].InRunQueue) {
        RunQueueEnqueue(SchedulerOpsGet()->PickHome(&gTasks[Child]), &gTasks[Child]);
    }
    SpinLockRelease(&gSchedulerLock);
    return 0;
}
