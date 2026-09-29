/*
 * SchedulerThread.c — PR-U-thread-1：同 VAS 线程槽（尚无用户 syscall）
 */
#include "Scheduler.h"
#include "SchedulerOps.h"
#include "SchedulerPrivate.h"
#include "TaskFd.h"
#include "Hal.h"
#include "VirtualMemory.h"
#include "PhysicalMemory.h"
#include "SpinLock.h"

/* CreateThreadSpin 失败码（负返回值的绝对值） */
#define THR_SPIN_ERR_ARG    1
#define THR_SPIN_ERR_BUSY   2
#define THR_SPIN_ERR_NOMEM  3
#define THR_SPIN_ERR_NOVA   4
#define THR_SPIN_ERR_MAP    5
#define THR_SPIN_ERR_CREATE 6

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

int SchedulerCreateThread(TASK *Leader, const char *Name, UINT64 Rip, UINT64 Rsp) {
    int i;
    int Slot = -1;
    UINT8 *Top;
    HAL_INTERRUPT_FRAME *F;
    INT32 GroupId;
    INT32 LeaderId;
    int k;

    if (!Leader || !Leader->IsUser || !Leader->UserSpace || !Name) {
        return -1;
    }
    if (Leader->GroupId < 0) {
        return -1;
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
    HalFrameSetUserEntry(F, Rip, Rsp);

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
    RunQueueEnqueue(SchedulerOpsGet()->PickHome(&gTasks[Slot]), &gTasks[Slot]);
    SpinLockRelease(&gSchedulerLock);
    return Slot;
}

static int VaFreeInSpace(VIRTUAL_ADDRESS_SPACE *Space, UINT64 Va) {
    UINT64 Pte;

    if (!Space) {
        return 0;
    }
    Pte = HalPageGetEntry(Space->Root, Va);
    return !(Pte & HAL_PAGE_PRESENT);
}

/* 在 mmap 区找连续两页空穴；成功写 OutCode / OutStack */
static int FindSpinVas(VIRTUAL_ADDRESS_SPACE *Space, UINT64 *OutCode, UINT64 *OutStack) {
    UINT64 Va;

    for (Va = USER_MMAP_BASE; Va + 2 * PAGE_SIZE <= USER_MMAP_END; Va += PAGE_SIZE) {
        if (!VaFreeInSpace(Space, Va) || !VaFreeInSpace(Space, Va + PAGE_SIZE)) {
            continue;
        }
        *OutCode = Va;
        *OutStack = Va + PAGE_SIZE;
        return 0;
    }
    return -1;
}

int SchedulerCreateThreadSpin(TASK *Leader) {
    void *CodePage;
    void *StackPage;
    UINT64 CodeVa;
    UINT64 StackVa;
    UINT64 StackTop;
    UINT8 *Code;
    const char *Arch;
    int Slot;
    int i;

    if (!Leader || !Leader->IsUser || !Leader->UserSpace) {
        return -THR_SPIN_ERR_ARG;
    }

    /*
     * SMP：Leader 在别核 RUNNING 时改其页表不安全。
     * 请用 SLEEPDEMO（阻塞）或 UP；否则报 busy。
     */
    SpinLockAcquire(&gSchedulerLock);
    if (Leader->State == TASK_UNUSED || !Leader->UserSpace) {
        SpinLockRelease(&gSchedulerLock);
        return -THR_SPIN_ERR_ARG;
    }
    if (Leader->OnCpu >= 0 && Leader != CurrentTask()) {
        SpinLockRelease(&gSchedulerLock);
        return -THR_SPIN_ERR_BUSY;
    }
    SpinLockRelease(&gSchedulerLock);

    if (FindSpinVas(Leader->UserSpace, &CodeVa, &StackVa) != 0) {
        return -THR_SPIN_ERR_NOVA;
    }

    CodePage = PhysicalMemoryAllocatePage();
    StackPage = PhysicalMemoryAllocatePage();
    if (!CodePage || !StackPage) {
        if (CodePage) {
            PhysicalMemoryFreePage(CodePage);
        }
        if (StackPage) {
            PhysicalMemoryFreePage(StackPage);
        }
        return -THR_SPIN_ERR_NOMEM;
    }

    Code = (UINT8 *)CodePage;
    for (i = 0; i < (int)PAGE_SIZE; i++) {
        Code[i] = 0;
        ((UINT8 *)StackPage)[i] = 0;
    }
    Arch = HalArchName();
    if (Arch && Arch[0] == 'x') {
        Code[0] = 0xeb;
        Code[1] = 0xfe;
    } else if (Arch && Arch[0] == 'a') {
        Code[0] = 0x00;
        Code[1] = 0x00;
        Code[2] = 0x00;
        Code[3] = 0x14;
    } else {
        Code[0] = 0x6f;
        Code[1] = 0x00;
        Code[2] = 0x00;
        Code[3] = 0x00;
    }

    if (VirtualMemorySpaceMapPage(Leader->UserSpace, CodeVa, (UINT64)(UINTN)CodePage,
                                  PTE_PRESENT | PTE_USER | PTE_WRITABLE) != 0 ||
        VirtualMemorySpaceMapPage(Leader->UserSpace, StackVa, (UINT64)(UINTN)StackPage,
                                  PTE_PRESENT | PTE_USER | PTE_WRITABLE) != 0) {
        PhysicalMemoryFreePage(CodePage);
        PhysicalMemoryFreePage(StackPage);
        return -THR_SPIN_ERR_MAP;
    }

    {
        UINT64 Next = StackVa + PAGE_SIZE;

        Leader->MmapNext = Next;
        for (i = 0; i < MAX_TASKS; i++) {
            if (gTasks[i].State == TASK_UNUSED || !gTasks[i].IsUser) {
                continue;
            }
            if (gTasks[i].GroupId == Leader->GroupId) {
                gTasks[i].MmapNext = Next;
            }
        }
    }

    StackTop = StackVa + PAGE_SIZE;
    Slot = SchedulerCreateThread(Leader, "thspin", CodeVa, StackTop);
    if (Slot < 0) {
        return -THR_SPIN_ERR_CREATE;
    }
    return Slot;
}
