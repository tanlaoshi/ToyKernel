/*
 * SchedulerPreempt.c — PR-K-preempt-cs：嵌套 PreemptCount
 * OnTimer 在 count≠0 时不切任务（为日后内核 IF=1 铺路）。
 */
#include "Scheduler.h"
#include "SchedulerPrivate.h"
#include "Hal.h"

/* 每核计数：定时器按核投递；嵌套 Disable/Enable 须同核配对 */
static int gPreemptCount[HAL_MAX_CPUS];

void SchedulerPreemptDisable(void) {
    UINT32 Cpu = HalGetCpuId();

    if (Cpu >= HAL_MAX_CPUS) {
        Cpu = 0;
    }
    gPreemptCount[Cpu]++;
}

void SchedulerPreemptEnable(void) {
    UINT32 Cpu = HalGetCpuId();

    if (Cpu >= HAL_MAX_CPUS) {
        Cpu = 0;
    }
    if (gPreemptCount[Cpu] > 0) {
        gPreemptCount[Cpu]--;
    }
}

int SchedulerPreemptCount(void) {
    UINT32 Cpu = HalGetCpuId();

    if (Cpu >= HAL_MAX_CPUS) {
        Cpu = 0;
    }
    return gPreemptCount[Cpu];
}
