/*
 * SchedulerPriv.h — PR-S-sched-split-1：调度器内部（仅 Common/Core；User 勿 include）
 */
#ifndef SCHEDULER_PRIV_H
#define SCHEDULER_PRIV_H

#include "Scheduler.h"
#include "SpinLock.h"

/* 定义在 Scheduler.c */
extern TASK *gIdleTask[HAL_MAX_CPUS];
extern SPIN_LOCK gSchedulerLock;

TASK *CurrentTask(void);
void SetCurrentTask(TASK *T);
int IsIdleTask(const TASK *T);

/* 定义在 SchedRunq.c */
void RunqInit(void);
void RunqEnqueue(UINT32 Cpu, TASK *T);
void RunqRemove(TASK *T);
UINT32 PickHomeCpu(const TASK *T);
TASK *PickNext(UINT32 Cpu);

#endif
