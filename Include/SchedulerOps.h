/*
 * SchedulerOps.h — 可替换调度政策。框架持锁后调用 Enqueue/Remove。
 */
#ifndef SCHEDULER_OPS_H
#define SCHEDULER_OPS_H

#ifdef TOY_SCHED_HOST
#include "SchedHostTypes.h"
#else
#include "Scheduler.h"
#endif

typedef struct {
    void (*Init)(void);
    void (*Enqueue)(UINT32 Cpu, TASK *T);
    void (*Remove)(TASK *T);
    UINT32 (*PickHome)(const TASK *T);
    TASK *(*PickNext)(UINT32 Cpu);
} SCHEDULER_OPS;

void SchedulerOpsRegister(const SCHEDULER_OPS *Ops);
const SCHEDULER_OPS *SchedulerOpsGet(void);
const SCHEDULER_OPS *SchedulerRoundRobinOps(void);
const SCHEDULER_OPS *SchedulerPriorityOps(void);

#endif
