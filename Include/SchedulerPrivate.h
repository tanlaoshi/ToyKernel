/*
 * SchedulerPrivate.h — 调度器内部（仅 Common/Core；User 勿 include）
 *
 * PR-S-sched-split-1：runq
 * PR-S-sched-split-2：wait/exit 共享符号
 */
#ifndef SCHEDULER_PRIVATE_H
#define SCHEDULER_PRIVATE_H

#include "Scheduler.h"
#include "SpinLock.h"

/* 定义在 Scheduler.c */
extern TASK gTasks[MAX_TASKS];
extern int gTaskCount;
extern TASK *gIdleTask[HAL_MAX_CPUS];
extern SPIN_LOCK gSchedulerLock;
extern int gCoopDrain;
extern volatile int gSchedulerOnline;

#define SIG_HANDLER_DFL 0ULL
#define SIG_HANDLER_IGN 1ULL

void CopyName(TASK *T, const char *Name);
void IdleTask(void);
TASK *FindRunnable(UINT32 Cpu);
int SignalDefaultTerminates(INT32 Sig);
UINT64 *SignalHandlerSlot(TASK *T, INT32 Sig);
UINT64 SignalHandlerGet(TASK *T, INT32 Sig);
int DeliverToHandlerFrame(TASK *T, HAL_INTERRUPT_FRAME *F, UINT64 Handler, INT32 Sig);
int DeliverKillLocked(TASK *T, INT32 Sig, int *ShowPrompt,
                      VIRTUAL_ADDRESS_SPACE **OutSpace,
                      HAL_INTERRUPT_FRAME *LiveFrame);

TASK *CurrentTask(void);
void SetCurrentTask(TASK *T);
int IsIdleTask(const TASK *T);
INT32 TaskSlot(const TASK *T);
void ActivateTask(TASK *T);
UINT64 SchedulerResumeFrame(TASK *T);

/* 定义在 SchedulerRunq.c */
void RunQueueInitialize(void);
void RunQueueEnqueue(UINT32 Cpu, TASK *T);
void RunQueueRemove(TASK *T);
UINT32 PickHomeCpu(const TASK *T);
TASK *PickNext(UINT32 Cpu);

/* 定义在 SchedulerWait.c */
int TerminateUserLocked(TASK *Exiting, INT32 Code, int *ShowPrompt,
                        VIRTUAL_ADDRESS_SPACE **OutSpace);
void SchedulerDestroyDetached(VIRTUAL_ADDRESS_SPACE *Space);

#endif
