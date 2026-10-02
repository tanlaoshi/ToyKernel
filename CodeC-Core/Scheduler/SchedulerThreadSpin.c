/*
 * SchedulerThreadSpin.c — thr-1/2 调试：映 spin 入口 + CreateThread
 */
#include "Scheduler.h"
#include "SchedulerPrivate.h"
#include "SchedulerOps.h"
#include "Hal.h"
#include "VirtualMemory.h"
#include "PhysicalMemory.h"
#include "SpinLock.h"

#define THR_SPIN_ERR_ARG    1
#define THR_SPIN_ERR_BUSY   2
#define THR_SPIN_ERR_NOMEM  3
#define THR_SPIN_ERR_NOVA   4
#define THR_SPIN_ERR_MAP    5
#define THR_SPIN_ERR_CREATE 6

int SchedulerCreateThreadSpin(TASK *Leader) {
    void *CodePage;
    UINT64 CodeVa;
    UINT8 *Code;
    const char *Arch;
    int Slot;
    int i;
    UINT64 Va;

    if (!Leader || !Leader->IsUser || !Leader->UserSpace) {
        return -THR_SPIN_ERR_ARG;
    }

    /*
     * Leader 在别核 RUNNING 时改页表不安全。短等几拍（UP/刚切走常见）；
     * 仍 busy 则让调用方换 TOY_SMP=1 或 THREADSMOKE。
     */
    for (i = 0; i < 64; i++) {
        SpinLockAcquire(&gSchedulerLock);
        if (Leader->State == TASK_UNUSED || !Leader->UserSpace) {
            SpinLockRelease(&gSchedulerLock);
            return -THR_SPIN_ERR_ARG;
        }
        if (Leader->OnCpu < 0 || Leader == CurrentTask()) {
            SpinLockRelease(&gSchedulerLock);
            break;
        }
        SpinLockRelease(&gSchedulerLock);
        HalCpuRelax();
        (void)SchedulerCondResched();
    }
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

    CodeVa = 0;
    for (Va = USER_MMAP_BASE; Va + PAGE_SIZE <= USER_MMAP_END; Va += PAGE_SIZE) {
        UINT64 Pte = HalPageGetEntry(Leader->UserSpace->Root, Va);
        if (!(Pte & HAL_PAGE_PRESENT)) {
            CodeVa = Va;
            break;
        }
    }
    if (CodeVa == 0) {
        return -THR_SPIN_ERR_NOVA;
    }

    CodePage = PhysicalMemoryAllocatePage();
    if (!CodePage) {
        return -THR_SPIN_ERR_NOMEM;
    }
    Code = (UINT8 *)CodePage;
    for (i = 0; i < (int)PAGE_SIZE; i++) {
        Code[i] = 0;
    }
    Arch = HalArchName();
    if (Arch && Arch[0] == 'x') {
        Code[0] = 0xeb;
        Code[1] = 0xfe; /* jmp $ — TLS 由内核字段验收，勿依赖 FS 首入 */
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
                                  PTE_PRESENT | PTE_USER | PTE_WRITABLE) != 0) {
        PhysicalMemoryFreePage(CodePage);
        return -THR_SPIN_ERR_MAP;
    }
    Leader->MmapNext = CodeVa + PAGE_SIZE;

    /* Rsp=0 → thr-2 自动映栈 + TLS */
    Slot = SchedulerCreateThread(Leader, "thspin", CodeVa, 0, 0, 0, 0);
    if (Slot < 0) {
        return -THR_SPIN_ERR_CREATE;
    }
    return Slot;
}

void SchedulerDropDiagThread(int Slot) {
    TASK *T;

    if (Slot < 0 || Slot >= MAX_TASKS) {
        return;
    }
    SpinLockAcquire(&gSchedulerLock);
    T = &gTasks[Slot];
    if (T->State == TASK_UNUSED || !T->IsUser || !T->IsThread) {
        SpinLockRelease(&gSchedulerLock);
        return;
    }
    SchedulerOpsGet()->Remove(T);
    T->UserSpace = 0; /* 共享 VAS 留给组主 */
    T->State = TASK_UNUSED;
    T->Frame = 0;
    T->PageRoot = 0;
    T->IsUser = 0;
    T->Started = 0;
    T->ParentId = -1;
    T->GroupId = -1;
    T->LeaderId = -1;
    T->IsThread = 0;
    T->TlsBase = 0;
    T->JoinerSlot = -1;
    T->JoinTid = -1;
    T->Waiting = 0;
    T->OnCpu = -1;
    T->InRunQueue = 0;
    gTaskCount--;
    SpinLockRelease(&gSchedulerLock);
}
