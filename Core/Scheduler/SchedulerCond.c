/*
 * SchedulerCond.c — PR-K-preempt-needresched：NeedResched + CondResched
 *
 * 禁切段内 OnTimer 只置位；安全点 CondResched 开 IF + hlt，由 OnTimer 浅栈切核。
 * 勿用 int 0x80：AP idle 上造 Frame 会 #UD（rip 垃圾）。
 */
#include "Scheduler.h"
#include "SchedulerPrivate.h"
#include "Hal.h"

static int gNeedResched[HAL_MAX_CPUS];

void SchedulerSetNeedResched(void) {
    UINT32 Cpu = HalGetCpuId();

    if (Cpu >= HAL_MAX_CPUS) {
        Cpu = 0;
    }
    gNeedResched[Cpu] = 1;
}

int SchedulerCondResched(void) {
    UINT32 Cpu = HalGetCpuId();
    TASK *Cur;

    if (Cpu >= HAL_MAX_CPUS) {
        Cpu = 0;
    }
    if (SchedulerPreemptCount() != 0) {
        return 0;
    }
    if (!gNeedResched[Cpu]) {
        return 0;
    }
    gNeedResched[Cpu] = 0;

    Cur = CurrentTask();
    if (!Cur || Cur->IsUser) {
        return 0;
    }

    /*
     * 浅栈让步：sti;hlt → 定时器 OnTimer 走既有 PickNext（KernelEnter 仍 jmp）。
     * 切走后本任务被再调度时从 hlt 返回。
     */
    HalIrqEnable();
    HalCpuHalt();
    return 1;
}
