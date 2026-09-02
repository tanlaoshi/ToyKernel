/*
 * Scheduler.h — 抢占式任务调度器接口
 */
#ifndef SCHEDULER_H
#define SCHEDULER_H

#include "BootTypes.h"
#include "VirtualMemory.h"
#include "Hal.h"

#define MAX_TASKS 8
#define MAX_FDS   4
#define FD_MAX_BYTES (64 * 1024)

typedef enum {
    TASK_UNUSED = 0,
    TASK_READY,
    TASK_RUNNING,
    TASK_BLOCKED,
    TASK_ZOMBIE,
} TASK_STATE;

typedef struct {
    int     Used;
    UINT8  *Data;
    UINTN   Size;
    UINTN   Pos;
    UINT32  Pages;
    char    Path[16];
    int     Dirty;
} TASK_FD;

typedef struct TASK {
    UINT8                  Stack[8192] __attribute__((aligned(16)));
    HAL_FRAME             *Frame;
    TASK_STATE             State;
    UINT32                 Id;
    UINT32                 Ticks;
    char                   Name[16];
    UINT64                 PageRoot;   /* 页表根物理地址（x86 曾称 CR3） */
    int                    IsUser;
    int                    Started;
    VM_ADDR_SPACE         *UserSpace;
    INT32                  ParentId;   /* -1 = 无父进程 */
    INT32                  ExitCode;
    int                    Waiting;    /* wait() 阻塞中 */
    TASK_FD                Fds[MAX_FDS];
} TASK;

void SchedulerInit(void);
int SchedulerCreate(const char *Name, void (*Entry)(void));
int SchedulerCreateUser(const char *Name, UINT64 Rip, UINT64 Rsp, UINT64 PageRoot,
                    VM_ADDR_SPACE *Space);
UINT64 SchedulerOnTimer(HAL_FRAME *Frame);
UINT64 SchedulerExitUser(HAL_FRAME *Frame);
UINT64 SchedulerFork(HAL_FRAME *Frame);
UINT64 SchedulerWait(HAL_FRAME *Frame);
UINT64 SchedulerYield(HAL_FRAME *Frame);
void SchedulerStart(void);

void SchedulerFdCloseAll(TASK *T);
int SchedulerFdOpen(TASK *T, const char *Path);
int SchedulerFdRead(TASK *T, int Fd, void *Buf, UINTN Len);
int SchedulerFdWrite(TASK *T, int Fd, const void *Buf, UINTN Len);
int SchedulerFdClose(TASK *T, int Fd);

void SchedulerReapOrphanZombies(void);

TASK *SchedulerCurrent(void);
int SchedulerTaskCount(void);
const TASK *SchedulerTaskByIndex(int Index);
UINT64 SchedulerTaskRip(const TASK *T);

#endif
