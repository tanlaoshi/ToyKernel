/*
 * TaskFd.h — 调度器内部 FD 辅助（PR-R3）
 * 公开 SchedulerFd* 仍在 Scheduler.h。
 */
#ifndef TASK_FD_H
#define TASK_FD_H

#include "Scheduler.h"

void TaskClearFds(TASK *T);
void TaskCloneFds(TASK *Child, TASK *Parent);

#endif
