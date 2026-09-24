/*
 * SchedulerSleep.c — 用户态 sleep：原地 sti+hlt 等节拍（不切到 shell）
 *
 * 契约：HalCpuTicks 经 HalTicksPerSec 换成墙钟毫秒（QEMU/真机拍长不同）。
 * 不调度走：内核 shell/gui 协作态 IF=0，切过去会吃不到 timer / 误 swapgs。
 * OnTimer 见 SchedulerSignal.c：SleepWakeTick≠0 且未到期则不抢占。
 */
#include "SchedulerPrivate.h"
#include "SchedulerOps.h"
#include "Hal.h"

#define SLEEP_MS_MAX 60000u

void SchedulerWakeSleepers(void) {
    UINT64 Now;
    int i;

    if (!gSchedulerOnline) {
        return;
    }

    Now = HalCpuTicks(0);
    SpinLockAcquire(&gSchedulerLock);
    for (i = 0; i < MAX_TASKS; i++) {
        TASK *T = &gTasks[i];

        if (T->State != TASK_BLOCKED || T->SleepWakeTick == 0) {
            continue;
        }
        if (Now < T->SleepWakeTick) {
            continue;
        }
        T->SleepWakeTick = 0;
        T->Waiting = 0;
        T->State = TASK_READY;
        if (T->Frame) {
            HalFrameSetReturn(T->Frame, 0);
        }
        RunQueueEnqueue(SchedulerOpsGet()->PickHome(T), T);
    }
    SpinLockRelease(&gSchedulerLock);
}

UINT64 SchedulerSleepMs(HAL_INTERRUPT_FRAME *Frame, UINT32 Ms) {
    TASK *Self;
    UINT64 Now;
    UINT64 Wake;
    UINT32 Tps;

    if (Ms == 0) {
        HalFrameSetReturn(Frame, 0);
        return SchedulerYield(Frame);
    }
    if (Ms > SLEEP_MS_MAX) {
        Ms = SLEEP_MS_MAX;
    }

    Self = CurrentTask();
    if (Self == 0) {
        HalFrameSetReturn(Frame, 0);
        return 0;
    }

    Tps = HalTicksPerSec();
    if (Tps == 0) {
        Tps = 1000;
    }
    Now = HalCpuTicks(0);
    Wake = Now + ((UINT64)Ms * (UINT64)Tps) / 1000ULL;
    if (Wake <= Now) {
        Wake = Now + 1;
    }

    /*
     * 保持 RUNNING：标记 SleepWakeTick，OnTimer 未到期不抢占。
     * sti+hlt 让节拍前进；timer 可能改写 Cur->Frame，返回前复原。
     */
    Self->Waiting = 0;
    Self->SleepWakeTick = Wake;
    HalFrameSetReturn(Frame, 0);

    while (HalCpuTicks(0) < Wake) {
        HalIrqEnable();
        HalCpuHalt();
        HalIrqDisable();
    }

    Self->SleepWakeTick = 0;
    Self->Frame = Frame;
    return 0;
}
