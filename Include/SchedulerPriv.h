/*
 * SchedulerPriv.h — 调度器内部（仅 Common/Core；User 勿 include）
 *
 * PR-S-sched-split-1：runq
 * PR-S-sched-split-2：wait/exit 共享符号
 */
#ifndef SCHEDULER_PRIV_H
#define SCHEDULER_PRIV_H

#include "Scheduler.h"
#include "SpinLock.h"

/* 定义在 Scheduler.c */
extern TASK gTasks[MAX_TASKS];
extern int gTaskCount;
extern TASK *gIdleTask[HAL_MAX_CPUS];
extern SPIN_LOCK gSchedulerLock;
extern int gCoopDrain;

TASK *CurrentTask(void);
void SetCurrentTask(TASK *T);
int IsIdleTask(const TASK *T);
INT32 TaskSlot(const TASK *T);
void ActivateTask(TASK *T);
UINT64 SchedResumeFrame(TASK *T);

/* 定义在 SchedRunq.c */
void RunqInit(void);
void RunqEnqueue(UINT32 Cpu, TASK *T);
void RunqRemove(TASK *T);
UINT32 PickHomeCpu(const TASK *T);
TASK *PickNext(UINT32 Cpu);

/* 定义在 SchedWait.c */
int TerminateUserLocked(TASK *Exiting, INT32 Code, int *ShowPrompt,
                        VIRTUAL_ADDRESS_SPACE **OutSpace);
void SchedDestroyDetached(VIRTUAL_ADDRESS_SPACE *Space);

#endif
