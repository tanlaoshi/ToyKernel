/*
 * SchedulerTerminate.c — PR-U-thread-3：收尸 / TerminateUserLocked / join 唤醒
 *
 * 从 SchedulerWait.c 拆出（守 ≤300）；Wait/Exit 仍在 Wait.c。
 */
#include "SchedulerPrivate.h"
#include "SchedulerOps.h"
#include "TaskFd.h"
#include "Hal.h"
#include "VirtualMemory.h"

static void ReapZombie(TASK *Z) {
    SchedulerOpsGet()->Remove(Z);
    SchedulerFdCloseAll(Z);
    /*
     * TerminateUserLocked 通常已卸 UserSpace；若仍挂着，仅当组内最后一份才 Destroy。
     */
    if (Z->UserSpace) {
        if (SchedulerGroupAliveCount(Z->GroupId) <= 1) {
            VirtualMemorySpaceDestroy(Z->UserSpace);
        }
        Z->UserSpace = 0;
    }
    Z->State = TASK_UNUSED;
    Z->Frame = 0;
    Z->PageRoot = 0;
    Z->IsUser = 0;
    Z->Started = 0;
    Z->ParentId = -1;
    Z->GroupId = -1;
    Z->LeaderId = -1;
    Z->IsThread = 0;
    Z->TlsBase = 0;
    Z->JoinerSlot = -1;
    Z->JoinTid = -1;
    Z->Waiting = 0;
    Z->SleepWakeTick = 0;
    Z->PendingKill = 0;
    Z->OnCpu = -1;
    Z->InRunQueue = 0;
    gTaskCount--;
}

void SchedulerReapZombie(TASK *Z) {
    if (Z) {
        ReapZombie(Z);
    }
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
    RunQueueEnqueue(SchedulerOpsGet()->PickHome(P), P);
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
    int LastLive;
    int i;

    if (OutSpace) {
        *OutSpace = 0;
    }
    if (!Exiting || !Exiting->IsUser) {
        return 0;
    }
    if (ShowPrompt) {
        *ShowPrompt = 0;
    }

    /* 活线程（不含已退 ZOMBIE）；末活线程收官整组 */
    LastLive = (SchedulerGroupLiveCount(Exiting->GroupId) <= 1);

    SchedulerFdCloseAll(Exiting);
    if (LastLive) {
        /* 先收同组未 join 的线程尸 */
        for (i = 0; i < MAX_TASKS; i++) {
            TASK *Z = &gTasks[i];
            if (Z == Exiting || Z->State != TASK_ZOMBIE || !Z->IsUser) {
                continue;
            }
            if (Z->GroupId != Exiting->GroupId) {
                continue;
            }
            Z->UserSpace = 0;
            ReapZombie(Z);
        }
        if (OutSpace) {
            *OutSpace = Exiting->UserSpace;
        } else if (Exiting->UserSpace) {
            VirtualMemorySpaceDestroy(Exiting->UserSpace);
        }
    }
    Exiting->UserSpace = 0;
    Exiting->ExitCode = Code;
    Exiting->PageRoot = VirtualMemoryKernelRoot();
    Exiting->Waiting = 0;
    Exiting->SleepWakeTick = 0;
    Exiting->PendingKill = 0;
    Exiting->OnCpu = -1;
    Exiting->JoinTid = -1;
    SchedulerOpsGet()->Remove(Exiting);

    if (!LastLive) {
        /* 非末活：变 joinable zombie；若有 joiner 则唤醒并收尸 */
        TASK *J;
        INT32 Js = Exiting->JoinerSlot;

        Exiting->State = TASK_ZOMBIE;
        Exiting->JoinerSlot = -1;
        if (Js >= 0 && Js < MAX_TASKS) {
            J = &gTasks[Js];
            if (J->State == TASK_BLOCKED && J->JoinTid == (INT32)Exiting->Id && J->Frame) {
                HalFrameSetReturn2(J->Frame, 0, (UINT64)(UINT32)Code);
                J->Waiting = 0;
                J->JoinTid = -1;
                J->State = TASK_READY;
                RunQueueEnqueue(SchedulerOpsGet()->PickHome(J), J);
                ReapZombie(Exiting);
            }
        }
        return Exiting == CurrentTask() ? 1 : 0;
    }

    if (ParentIsUserWaiter(Exiting->ParentId)) {
        Exiting->State = TASK_ZOMBIE;
        Exiting->JoinerSlot = -1;
        if (!WakeWaitingParent(Exiting)) {
            /* 父稍后 wait */
        }
    } else {
        if (ShowPrompt) {
            *ShowPrompt = 1;
        }
        Exiting->State = TASK_UNUSED;
        Exiting->Frame = 0;
        Exiting->ParentId = -1;
        Exiting->GroupId = -1;
        Exiting->LeaderId = -1;
        Exiting->IsThread = 0;
        Exiting->TlsBase = 0;
        Exiting->JoinerSlot = -1;
        Exiting->JoinTid = -1;
        gTaskCount--;
    }

    return Exiting == CurrentTask() ? 1 : 0;
}

void SchedulerDestroyDetached(VIRTUAL_ADDRESS_SPACE *Space) {
    if (Space) {
        VirtualMemorySpaceDestroy(Space);
    }
}
