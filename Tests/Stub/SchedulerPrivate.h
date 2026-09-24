/*
 * SchedulerPrivate.h — Host 桩声明。真实框架符号见 Include/SchedulerPrivate.h。
 */
#ifndef SCHEDULER_PRIVATE_H
#define SCHEDULER_PRIVATE_H

#include "SchedHostTypes.h"

int HalCpuCount(void);
void StubSetCpuCount(int Cpus);
int IsIdleTask(const TASK *T);
void SchedulerRunQueueView(UINT32 Cpu, TASK ***Slots, int **Count);
void RunQueueRemove(TASK *T);
TASK *PickNext(UINT32 Cpu);

#endif
