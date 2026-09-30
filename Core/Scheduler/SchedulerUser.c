/*
 * SchedulerUser.c — kill / signal / yield（PR-S3-scheduser-1）
 *
 * fork 见 SchedulerFork.c。
 */
#include "Scheduler.h"
#include "SchedulerOps.h"
#include "SchedulerPrivate.h"
#include "Syscall.h"
#include "Hal.h"
#include "Console.h"
#include "VirtualMemory.h"
#include "SpinLock.h"

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
    /*
     * PR-TEST：自定义 handler（>SIG_HANDLER_IGN）必须落在用户代码区
     * （≥ USER_CODE_VIRT）。拒绝 (sighandler_t)2 这类 null 页非法值——
     * 否则 signal() 返回成功，投递时才在 DeliverToHandlerFrame 崩。
     * SIG_DFL=0 / SIG_IGN=1 仍放行；真实 handler（如 SigDemo 的 OnTerm
     * 在 0x40000000+）通过。
     */
    if (Handler > SIG_HANDLER_IGN && Handler < USER_CODE_VIRT) {
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
