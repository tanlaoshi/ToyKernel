/*
 * Scheduler.h — 抢占式任务调度器接口
 */
#ifndef SCHEDULER_H
#define SCHEDULER_H

#include "BootTypes.h"
#include "VirtualMemory.h"
#include "Hal.h"
#include "Fat.h"

#define MAX_TASKS 16
#define MAX_FDS   8
#define FD_MAX_BYTES (64 * 1024)

#define FD_KIND_FILE   0
#define FD_KIND_SOCKET 1
#define FD_KIND_PIPE   2
#define FD_KIND_DIR    3  /* PR-F4：目录快照（Data=FAT_DIRECTORY_ENTRY[]） */

#define PIPE_END_READ  0
#define PIPE_END_WRITE 1

typedef enum {
    TASK_UNUSED = 0,
    TASK_READY,
    TASK_RUNNING,
    TASK_BLOCKED,
    TASK_ZOMBIE,
} TASK_STATE;

typedef struct {
    int     Used;
    int     Kind;   /* FD_KIND_FILE / SOCKET / PIPE */
    int     SockId; /* SOCKET=lwIP id；PIPE=PIPE_END_READ/WRITE */
    UINT8  *Data;   /* FILE=缓冲；PIPE=(PIPE*) 共享对象 */
    UINTN   Size;
    UINTN   Pos;
    UINT32  Pages;
    char    Path[64];
    int     Dirty;
} TASK_FD;

typedef struct TASK {
    UINT8                  Stack[8192] __attribute__((aligned(16)));
    HAL_INTERRUPT_FRAME             *Frame;
    TASK_STATE             State;
    UINT32                 Id;
    UINT32                 Ticks;
    char                   Name[16];
    UINT64                 PageRoot;   /* 页表根物理地址（x86 曾称 CR3） */
    int                    IsUser;
    int                    Started;
    VIRTUAL_ADDRESS_SPACE         *UserSpace;
    INT32                  ParentId;   /* -1 = 无父进程 */
    INT32                  ExitCode;
    int                    Waiting;    /* wait() 阻塞中 */
    INT32                  PendingKill; /* PR-P4：>0 待默认终止（跨核 RUNNING） */
    INT32                  Affinity;   /* -1=任意 CPU；否则逻辑 CpuId */
    INT32                  OnCpu;      /* 正在跑的逻辑 CPU；未跑为 -1 */
    INT32                  HomeCpu;    /* 首选运行队列（PR-S4） */
    INT32                  Priority;   /* PR-S-lock：越大越优先；默认 0；idle 最低 */
    int                    InRunq;     /* 已在某核 READY 队列中 */
    UINT64                 BrkBase;    /* 映像数据/BSS 末；不可低于此（PR-P3） */
    UINT64                 Brk;        /* 当前 program break */
    TASK_FD                Fds[MAX_FDS];
} TASK;

#define SCHED_PRIORITY_DEFAULT  0
#define SCHED_PRIORITY_SHELL    8   /* shell / gui：交互偏高 */
#define SCHED_PRIORITY_IDLE   (-128)

void SchedulerInit(void);
int SchedulerCreate(const char *Name, void (*Entry)(void));
int SchedulerCreateUser(const char *Name, UINT64 Rip, UINT64 Rsp, UINT64 PageRoot,
                    VIRTUAL_ADDRESS_SPACE *Space, UINT64 BrkBase);
void SchedulerSetAffinity(int TaskId, INT32 Cpu);
/* PR-S-lock：pid=槽位+1（与 kill/ps 一致）；成功 0，失败 -1 */
int SchedulerSetPriority(INT32 Pid, INT32 Priority);
UINT64 SchedulerOnTimer(HAL_INTERRUPT_FRAME *Frame);
UINT64 SchedulerExitUser(HAL_INTERRUPT_FRAME *Frame);
UINT64 SchedulerFork(HAL_INTERRUPT_FRAME *Frame);
UINT64 SchedulerWait(HAL_INTERRUPT_FRAME *Frame);
UINT64 SchedulerYield(HAL_INTERRUPT_FRAME *Frame);
/* PR-P4：rdi=pid rsi=sig；杀内核/idle 失败。非当前任务返回 0；杀自身则切走 */
UINT64 SchedulerKill(HAL_INTERRUPT_FRAME *Frame);
/* Shell：pid=槽位+1；默认终止用户任务。成功 0，失败 -1 */
int SchedulerKillPid(INT32 Pid, INT32 Sig);
void SchedulerStart(void);
/* AP：等 BSP SchedulerStart 后进入本核 idle（不返回） */
void SchedulerApStart(void);
int SchedulerIsOnline(void);

void SchedulerFdCloseAll(TASK *T);
int SchedulerFdOpen(TASK *T, const char *Path);
/* PR-F4：打开目录并快照枚举；成功返回 dirfd */
int SchedulerFdOpenDirectory(TASK *T, const char *Path);
/* PR-F4：拷贝下一项到 Out；1=有项，0=结束，-1=失败 */
int SchedulerFdReadDirectory(TASK *T, int Fd, FAT_DIRECTORY_ENTRY *Out);
int SchedulerFdFileStat(TASK *T, const char *Path, FAT_FILE_STAT *Out);
int SchedulerFdSocket(TASK *T, int Domain, int Type, int Protocol);
int SchedulerFdBind(TASK *T, int Fd, UINT32 Ip, UINT16 Port);
int SchedulerFdListen(TASK *T, int Fd, int Backlog);
int SchedulerFdAccept(TASK *T, int Fd);
int SchedulerFdConnect(TASK *T, int Fd, UINT32 Ip, UINT16 Port);
int SchedulerFdRead(TASK *T, int Fd, void *Buf, UINTN Len);
int SchedulerFdWrite(TASK *T, int Fd, const void *Buf, UINTN Len);
int SchedulerFdClose(TASK *T, int Fd);
/* PR-P2：pipefd[0]=读端 pipefd[1]=写端；dup 复制槽位 */
int SchedulerFdPipe(TASK *T, int PipeFd[2]);
int SchedulerFdDup(TASK *T, int OldFd);

void SchedulerReapOrphanZombies(void);

/* PR-A12：virt 无定时抢占时，协作跑完就绪用户任务（exec HELLO） */
void SchedulerCoopDrainUsers(void);

TASK *SchedulerCurrent(void);
int SchedulerTaskCount(void);
UINT64 SchedulerStealCount(void);
const TASK *SchedulerTaskByIndex(int Index);
UINT64 SchedulerTaskRip(const TASK *T);

#endif
