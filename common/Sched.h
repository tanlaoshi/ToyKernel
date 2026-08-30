/*
 * Sched.h — 抢占式任务调度器接口
 */
#ifndef SCHED_H
#define SCHED_H

#include "BootConfig.h"
#include "Vmm.h"
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

void SchedInit(void);
int SchedCreate(const char *Name, void (*Entry)(void));
int SchedCreateUser(const char *Name, UINT64 Rip, UINT64 Rsp, UINT64 Cr3,
                    VM_ADDR_SPACE *Space);
UINT64 SchedOnTimer(struct INT_FRAME *Frame);
UINT64 SchedExitUser(struct INT_FRAME *Frame);
void SchedStart(void);

TASK *SchedCurrent(void);
int SchedTaskCount(void);
const TASK *SchedTaskByIndex(int Index);
UINT64 SchedTaskRip(const TASK *T);

#endif
