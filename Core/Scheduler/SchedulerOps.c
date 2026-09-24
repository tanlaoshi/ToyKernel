/*
 * SchedulerOps.c — 注册当前调度政策。实现仍在 SchedulerRunq.c。
 */
#include "SchedulerOps.h"
#include "SchedulerPrivate.h"

static const SCHEDULER_OPS *gOps;

void SchedulerOpsRegister(const SCHEDULER_OPS *Ops)
{
    gOps = Ops;
}

const SCHEDULER_OPS *SchedulerOpsGet(void)
{
    return gOps;
}

const SCHEDULER_OPS *SchedulerRoundRobinOps(void)
{
    static const SCHEDULER_OPS Ops = {
        RunQueueInitialize,
        RunQueueEnqueue,
        RunQueueRemove,
        PickHomeCpu,
        PickNext,
    };
    return &Ops;
}
