/*
 * SchedulerOps.c — 注册当前调度政策。
 */
#include "SchedulerOps.h"

static const SCHEDULER_OPS *gOps;

void SchedulerOpsRegister(const SCHEDULER_OPS *Ops)
{
    gOps = Ops;
}

const SCHEDULER_OPS *SchedulerOpsGet(void)
{
    return gOps;
}
