/*
 * SchedulerSignal.c — 定时器与信号投递（PR-S-sched-1）
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

TASK *FindRunnable(UINT32 Cpu) {
    return SchedulerOpsGet()->PickNext(Cpu);
}

int SignalDefaultTerminates(INT32 Sig) {
    return Sig == SIGKILL || Sig == SIGTERM || Sig == SIGINT;
}

UINT64 *SignalHandlerSlot(TASK *T, INT32 Sig) {
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

UINT64 SignalHandlerGet(TASK *T, INT32 Sig) {
    UINT64 *Slot = SignalHandlerSlot(T, Sig);
    return Slot ? *Slot : SIG_HANDLER_DFL;
}

/* 把用户帧改成进入 handler；成功 0 */
int DeliverToHandlerFrame(TASK *T, HAL_INTERRUPT_FRAME *F, UINT64 Handler,
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
int DeliverKillLocked(TASK *T, INT32 Sig, int *ShowPrompt,
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

    /* 原地 sleep：未到期不抢占；并唤醒已 BLOCKED 的 sleep 者（若有） */
    SchedulerWakeSleepers();
    if (Cur->SleepWakeTick != 0 && HalCpuTicks(0) < Cur->SleepWakeTick) {
        return 0;
    }

    /* 禁切段：只置 NeedResched；出段后 CondResched 浅 hlt 再让 OnTimer 切 */
    if (SchedulerPreemptCount() != 0) {
        SchedulerSetNeedResched();
        return 0;
    }

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
                SchedulerDestroyDetached(Detached);
                ConsoleWrite("sched: no runnable after pending kill\n");
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
        } else {
            SpinLockRelease(&gSchedulerLock);
            SchedulerDestroyDetached(Detached);
        }
    }

    /* PR-S-runq：普通抢占只持每核 runq 锁，两核可并行 PickNext */
    Next = SchedulerOpsGet()->PickNext(Cpu);
    if (Next == Cur || Next == 0) {
        return 0;
    }
    ActivateTask(Next);
    return SchedulerResumeFrame(Next);
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
