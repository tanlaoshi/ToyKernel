/*
 * SchedulerThread.c — PR-U-thread：CreateThread + 用户栈/TLS（thr-1/2）
 */
#include "Scheduler.h"
#include "SchedulerOps.h"
#include "SchedulerPrivate.h"
#include "TaskFd.h"
#include "Hal.h"
#include "VirtualMemory.h"
#include "PhysicalMemory.h"
#include "SpinLock.h"

#define THREAD_STACK_PAGES  4u
#define THREAD_TLS_PAGES    1u

int SchedulerGroupAliveCount(INT32 GroupId) {
    int N = 0;
    int i;

    if (GroupId < 0) {
        return 0;
    }
    for (i = 0; i < MAX_TASKS; i++) {
        if (gTasks[i].State == TASK_UNUSED || !gTasks[i].IsUser) {
            continue;
        }
        if (gTasks[i].GroupId == GroupId) {
            N++;
        }
    }
    return N;
}

static int VaFree(VIRTUAL_ADDRESS_SPACE *Space, UINT64 Va) {
    UINT64 Pte = HalPageGetEntry(Space->Root, Va);
    return !(Pte & HAL_PAGE_PRESENT);
}

/* 在 mmap 区找连续 Pages 空页；成功 *OutBase=首 VA */
static int FindFreeRun(VIRTUAL_ADDRESS_SPACE *Space, UINT32 Pages, UINT64 *OutBase) {
    UINT64 Va;
    UINT32 Need = Pages * (UINT32)PAGE_SIZE;
    UINT32 n;

    if (!Space || Pages == 0 || !OutBase) {
        return -1;
    }
    for (Va = USER_MMAP_BASE; Va + Need <= USER_MMAP_END; Va += PAGE_SIZE) {
        for (n = 0; n < Pages; n++) {
            if (!VaFree(Space, Va + (UINT64)n * PAGE_SIZE)) {
                break;
            }
        }
        if (n == Pages) {
            *OutBase = Va;
            return 0;
        }
    }
    return -1;
}

static void AdvanceGroupMmap(INT32 GroupId, UINT64 Next) {
    int i;

    for (i = 0; i < MAX_TASKS; i++) {
        if (gTasks[i].State == TASK_UNUSED || !gTasks[i].IsUser) {
            continue;
        }
        if (gTasks[i].GroupId == GroupId) {
            gTasks[i].MmapNext = Next;
        }
    }
}

/* 映 Pages 匿名页；成功返回基址，失败 0 */
static UINT64 MapAnonRun(TASK *Owner, UINT32 Pages) {
    VIRTUAL_ADDRESS_SPACE *Space;
    UINT64 Base;
    UINT32 n;
    void *Page;

    if (!Owner || !Owner->UserSpace) {
        return 0;
    }
    Space = Owner->UserSpace;
    if (FindFreeRun(Space, Pages, &Base) != 0) {
        return 0;
    }
    for (n = 0; n < Pages; n++) {
        Page = PhysicalMemoryAllocatePage();
        if (!Page) {
            return 0;
        }
        {
            UINTN z;
            for (z = 0; z < PAGE_SIZE; z++) {
                ((UINT8 *)Page)[z] = 0;
            }
        }
        if (VirtualMemorySpaceMapPage(Space, Base + (UINT64)n * PAGE_SIZE,
                                      (UINT64)(UINTN)Page,
                                      PTE_PRESENT | PTE_USER | PTE_WRITABLE) != 0) {
            PhysicalMemoryFreePage(Page);
            return 0;
        }
    }
    AdvanceGroupMmap(Owner->GroupId, Base + (UINT64)Pages * PAGE_SIZE);
    return Base;
}

int SchedulerThreadEnsureTls(TASK *T) {
    UINT64 Base;
    UINT64 Tid;

    if (!T || !T->IsUser || !T->UserSpace) {
        return -1;
    }
    if (T->TlsBase != 0) {
        return 0;
    }
    Base = MapAnonRun(T, THREAD_TLS_PAGES);
    if (Base == 0) {
        return -1;
    }
    T->TlsBase = Base;
    Tid = (UINT64)T->Id;
    if (VirtualMemoryCopyToSpace(T->UserSpace, Base, &Tid, sizeof(Tid)) < 0) {
        return -1;
    }
    if (T->Frame) {
        HalFrameSetTls(T->Frame, Base);
    }
    return 0;
}

int SchedulerThreadAllocStack(TASK *Owner, UINT64 *OutTop) {
    UINT64 Base;

    if (!Owner || !OutTop) {
        return -1;
    }
    Base = MapAnonRun(Owner, THREAD_STACK_PAGES);
    if (Base == 0) {
        return -1;
    }
    *OutTop = Base + (UINT64)THREAD_STACK_PAGES * PAGE_SIZE;
    return 0;
}

int SchedulerCreateThread(TASK *Leader, const char *Name, UINT64 Rip, UINT64 Arg,
                          UINT64 Rsp) {
    int i;
    int Slot = -1;
    UINT8 *Top;
    HAL_INTERRUPT_FRAME *F;
    INT32 GroupId;
    INT32 LeaderId;
    int k;
    UINT64 StackTop = Rsp;

    if (!Leader || !Leader->IsUser || !Leader->UserSpace || !Name || Rip == 0) {
        return -1;
    }
    if (Leader->GroupId < 0) {
        return -1;
    }
    if (StackTop == 0) {
        if (SchedulerThreadAllocStack(Leader, &StackTop) != 0) {
            return -1;
        }
    }

    SpinLockAcquire(&gSchedulerLock);
    if (Leader->State == TASK_UNUSED || !Leader->UserSpace) {
        SpinLockRelease(&gSchedulerLock);
        return -1;
    }
    GroupId = Leader->GroupId;
    LeaderId = Leader->LeaderId;
    if (LeaderId < 0) {
        LeaderId = (INT32)Leader->Id;
    }

    for (i = 0; i < MAX_TASKS; i++) {
        if (gTasks[i].State == TASK_UNUSED) {
            Slot = i;
            break;
        }
    }
    if (Slot < 0) {
        SpinLockRelease(&gSchedulerLock);
        return -1;
    }

    Top = gTasks[Slot].Stack + sizeof(gTasks[Slot].Stack);
    F = (HAL_INTERRUPT_FRAME *)(Top - sizeof(HAL_INTERRUPT_FRAME));
    HalFrameSetUserEntry(F, Rip, StackTop);
    HalFrameSetArgument0(F, Arg);

    gTasks[Slot].Frame = F;
    gTasks[Slot].State = TASK_READY;
    gTasks[Slot].Ticks = 0;
    gTasks[Slot].PageRoot = Leader->PageRoot;
    gTasks[Slot].IsUser = 1;
    gTasks[Slot].Started = 0;
    gTasks[Slot].UserSpace = Leader->UserSpace;
    gTasks[Slot].ParentId = Leader->ParentId;
    gTasks[Slot].GroupId = GroupId;
    gTasks[Slot].LeaderId = LeaderId;
    gTasks[Slot].IsThread = 1;
    gTasks[Slot].TlsBase = 0;
    gTasks[Slot].ExitCode = 0;
    gTasks[Slot].Waiting = 0;
    gTasks[Slot].SleepWakeTick = 0;
    gTasks[Slot].PendingKill = 0;
    gTasks[Slot].SigHandlerInt = Leader->SigHandlerInt;
    gTasks[Slot].SigHandlerTerm = Leader->SigHandlerTerm;
    gTasks[Slot].Affinity = Leader->Affinity;
    gTasks[Slot].OnCpu = -1;
    gTasks[Slot].HomeCpu = Leader->HomeCpu;
    gTasks[Slot].Priority = Leader->Priority;
    gTasks[Slot].InRunQueue = 0;
    gTasks[Slot].BrkBase = Leader->BrkBase;
    gTasks[Slot].Brk = Leader->Brk;
    gTasks[Slot].MmapNext = Leader->MmapNext;
    for (k = 0; k < (int)sizeof(Leader->Cwd); k++) {
        gTasks[Slot].Cwd[k] = Leader->Cwd[k];
    }
    TaskCloneFds(&gTasks[Slot], Leader);
    CopyName(&gTasks[Slot], Name);
    gTaskCount++;
    /* 先不入队：EnsureTls 完成后再 READY，避免 FS=0 首入 #PF */
    SpinLockRelease(&gSchedulerLock);

    if (SchedulerThreadEnsureTls(&gTasks[Slot]) != 0) {
        SpinLockAcquire(&gSchedulerLock);
        gTasks[Slot].State = TASK_UNUSED;
        gTasks[Slot].Frame = 0;
        gTasks[Slot].UserSpace = 0;
        gTasks[Slot].GroupId = -1;
        gTasks[Slot].LeaderId = -1;
        gTasks[Slot].IsThread = 0;
        gTasks[Slot].TlsBase = 0;
        gTaskCount--;
        SpinLockRelease(&gSchedulerLock);
        return -1;
    }

    SpinLockAcquire(&gSchedulerLock);
    if (gTasks[Slot].State == TASK_READY && !gTasks[Slot].InRunQueue) {
        RunQueueEnqueue(SchedulerOpsGet()->PickHome(&gTasks[Slot]), &gTasks[Slot]);
    }
    SpinLockRelease(&gSchedulerLock);
    return Slot;
}
