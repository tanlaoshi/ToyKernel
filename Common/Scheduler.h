/*
 * Scheduler.h — 抢占式任务调度器接口
 */
#ifndef SCHEDULER_H
#define SCHEDULER_H

#include "BootConfig.h"
#include "VirtualMemory.h"
#include "hal.h"

#define MAX_TASKS 8

typedef enum {
    TASK_UNUSED = 0,
    TASK_READY,
    TASK_RUNNING,
} TASK_STATE;

typedef struct TASK {
    UINT8                  Stack[4096] __attribute__((aligned(16)));
    struct INT_FRAME      *Frame;
    TASK_STATE             State;
    UINT32                 Id;
    UINT32                 Ticks;
    char                   Name[16];
    UINT64                 Cr3;
    int                    IsUser;
    int                    Started;
    VM_ADDR_SPACE         *UserSpace;
} TASK;

void SchedulerInit(void);
int SchedulerCreate(const char *Name, void (*Entry)(void));
int SchedulerCreateUser(const char *Name, UINT64 Rip, UINT64 Rsp, UINT64 Cr3,
                    VM_ADDR_SPACE *Space);
UINT64 SchedulerOnTimer(struct INT_FRAME *Frame);
UINT64 SchedulerExitUser(struct INT_FRAME *Frame);
void SchedulerStart(void);

TASK *SchedulerCurrent(void);
int SchedulerTaskCount(void);
const TASK *SchedulerTaskByIndex(int Index);
UINT64 SchedulerTaskRip(const TASK *T);

#endif
