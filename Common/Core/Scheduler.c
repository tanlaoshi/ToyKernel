/*
 * Scheduler.c — 抢占式任务调度器（含 fork/wait 与每任务 FD）
 */
#include "Scheduler.h"
#include "Syscall.h"
#include "FileSystem.h"
#include "Hal.h"
#include "Console.h"
#include "Debug.h"
#include "VirtualMemory.h"
#include "PhysicalMemory.h"
#include "LwIp.h"
#include "Socket.h"
#include "SpinLock.h"

#define SCHED_TAG_KERNEL_FIRST 1ULL
#define SCHED_TAG_USER_FIRST   2ULL

static TASK gTasks[MAX_TASKS];
static TASK *gCurrentCpu[HAL_MAX_CPUS];
static TASK *gIdleTask[HAL_MAX_CPUS];
static SPIN_LOCK gSchedLock;
static volatile int gSchedOnline;
static int gTaskCount;
static int gRrHome;          /* 新建任务轮转 HomeCpu */
static UINT64 gStealCount;   /* 偷任务次数（调试/ps） */

typedef struct {
    TASK *Slot[MAX_TASKS];
    int   Count;
} CPU_RUNQ;

static CPU_RUNQ gRunq[HAL_MAX_CPUS];

static TASK *CurrentTask(void) {
    UINT32 Id = HalCpuId();
    if (Id >= HAL_MAX_CPUS) {
        return 0;
    }
    return gCurrentCpu[Id];
}

static void SetCurrentTask(TASK *T) {
    UINT32 Id = HalCpuId();
    if (Id < HAL_MAX_CPUS) {
        gCurrentCpu[Id] = T;
    }
}

static int IsIdleTask(const TASK *T) {
    UINT32 c;
    if (!T) {
        return 0;
    }
    for (c = 0; c < HAL_MAX_CPUS; c++) {
        if (gIdleTask[c] == T) {
            return 1;
        }
    }
    return 0;
}

static void RunqInit(void) {
    UINT32 c;
    int i;
    for (c = 0; c < HAL_MAX_CPUS; c++) {
        gRunq[c].Count = 0;
        for (i = 0; i < MAX_TASKS; i++) {
            gRunq[c].Slot[i] = 0;
        }
    }
}

static void RunqEnqueue(UINT32 Cpu, TASK *T) {
    CPU_RUNQ *Q;
    if (!T || Cpu >= HAL_MAX_CPUS || IsIdleTask(T) || T->InRunq) {
        return;
    }
    Q = &gRunq[Cpu];
    if (Q->Count >= MAX_TASKS) {
        return;
    }
    Q->Slot[Q->Count++] = T;
    T->InRunq = 1;
    T->HomeCpu = (INT32)Cpu;
}

static TASK *RunqDequeue(UINT32 Cpu) {
    CPU_RUNQ *Q;
    TASK *T;
    int i;
    if (Cpu >= HAL_MAX_CPUS) {
        return 0;
    }
    Q = &gRunq[Cpu];
    if (Q->Count <= 0) {
        return 0;
    }
    T = Q->Slot[0];
    for (i = 1; i < Q->Count; i++) {
        Q->Slot[i - 1] = Q->Slot[i];
    }
    Q->Count--;
    Q->Slot[Q->Count] = 0;
    if (T) {
        T->InRunq = 0;
    }
    return T;
}

/* 从队尾偷：减少与本地 dequeue 冲突的直觉（大锁下等价于任取） */
static TASK *RunqStealOne(UINT32 Victim) {
    CPU_RUNQ *Q;
    TASK *T;
    if (Victim >= HAL_MAX_CPUS) {
        return 0;
    }
    Q = &gRunq[Victim];
    if (Q->Count <= 0) {
        return 0;
    }
    T = Q->Slot[Q->Count - 1];
    Q->Count--;
    Q->Slot[Q->Count] = 0;
    if (T) {
        T->InRunq = 0;
    }
    return T;
}

static void RunqRemove(TASK *T) {
    UINT32 c;
    int i, j;
    if (!T || !T->InRunq) {
        return;
    }
    for (c = 0; c < HAL_MAX_CPUS; c++) {
        CPU_RUNQ *Q = &gRunq[c];
        for (i = 0; i < Q->Count; i++) {
            if (Q->Slot[i] != T) {
                continue;
            }
            for (j = i + 1; j < Q->Count; j++) {
                Q->Slot[j - 1] = Q->Slot[j];
            }
            Q->Count--;
            Q->Slot[Q->Count] = 0;
            T->InRunq = 0;
            return;
        }
    }
    T->InRunq = 0;
}

static UINT32 PickHomeCpu(const TASK *T) {
    int Cpus = HalCpuCount();
    UINT32 Home;
    if (Cpus < 1) {
        Cpus = 1;
    }
    if (Cpus > HAL_MAX_CPUS) {
        Cpus = HAL_MAX_CPUS;
    }
    if (T && T->Affinity >= 0 && T->Affinity < Cpus) {
        return (UINT32)T->Affinity;
    }
    Home = (UINT32)(gRrHome % Cpus);
    gRrHome++;
    return Home;
}

static INT32 TaskSlot(const TASK *T) {
    if (!T) {
        return -1;
    }
    return (INT32)(T - gTasks);
}

static void TaskClearFds(TASK *T) {
    int i;
    for (i = 0; i < MAX_FDS; i++) {
        T->Fds[i].Used = 0;
        T->Fds[i].Kind = FD_KIND_FILE;
        T->Fds[i].SockId = -1;
        T->Fds[i].Data = 0;
        T->Fds[i].Size = 0;
        T->Fds[i].Pos = 0;
        T->Fds[i].Pages = 0;
        T->Fds[i].Path[0] = 0;
        T->Fds[i].Dirty = 0;
    }
}

/* 管道对象放在单页前部，后随环形缓冲 */
typedef struct {
    UINTN Cap;
    UINTN Head;
    UINTN Tail;
    UINTN Len;
    int Readers;
    int Writers;
    UINT32 Pages;
    UINT8 *Buf;
} PIPE;

static PIPE *PipeFromFd(TASK_FD *F) {
    return (PIPE *)(UINTN)F->Data;
}

static void FdFlush(TASK_FD *F) {
    if (F->Used && F->Kind == FD_KIND_FILE && F->Dirty && F->Path[0] && F->Data) {
        (void)FsWriteFile(F->Path, F->Data, F->Size);
        F->Dirty = 0;
    }
}

static void FdCopyPath(TASK_FD *F, const char *Path) {
    int i;
    for (i = 0; i < (int)sizeof(F->Path) - 1 && Path[i]; i++) {
        F->Path[i] = Path[i];
    }
    F->Path[i] = 0;
}

static int FdAllocSlot(TASK *T) {
    int i;
    for (i = 0; i < MAX_FDS; i++) {
        if (!T->Fds[i].Used) {
            return i;
        }
    }
    return -1;
}

static void TaskCloneFds(TASK *Child, TASK *Parent) {
    int i;

    TaskClearFds(Child);
    for (i = 0; i < MAX_FDS; i++) {
        TASK_FD *S = &Parent->Fds[i];
        TASK_FD *D = &Child->Fds[i];
        if (!S->Used || S->Kind != FD_KIND_PIPE) {
            continue;
        }
        *D = *S;
        {
            PIPE *P = PipeFromFd(S);
            if (S->SockId == PIPE_END_READ) {
                P->Readers++;
            } else {
                P->Writers++;
            }
        }
    }
}

void SchedulerFdCloseAll(TASK *T) {
    int i;
    if (!T) {
        return;
    }
    for (i = 0; i < MAX_FDS; i++) {
        if (T->Fds[i].Used) {
            SchedulerFdClose(T, i);
        }
    }
}

int SchedulerFdOpen(TASK *T, const char *Path) {
    int Slot;
    UINT32 Pages;
    void *Buf;
    UINTN Size = 0;

    if (!T || !Path) {
        return -1;
    }
    Slot = FdAllocSlot(T);
    if (Slot < 0) {
        return -1;
    }
    Pages = (FD_MAX_BYTES + PAGE_SIZE - 1) / PAGE_SIZE;
    Buf = PhysicalMemoryAllocatePages(Pages);
    if (!Buf) {
        return -1;
    }
    if (FsReadFile(Path, Buf, FD_MAX_BYTES, &Size) != FAT_OK) {
        Size = 0;
    }
    T->Fds[Slot].Used = 1;
    T->Fds[Slot].Kind = FD_KIND_FILE;
    T->Fds[Slot].SockId = -1;
    T->Fds[Slot].Data = (UINT8 *)Buf;
    T->Fds[Slot].Size = Size;
    T->Fds[Slot].Pos = 0;
    T->Fds[Slot].Pages = Pages;
    FdCopyPath(&T->Fds[Slot], Path);
    T->Fds[Slot].Dirty = 0;
    return Slot;
}

int SchedulerFdSocket(TASK *T, int Domain, int Type, int Protocol) {
    int Slot = -1;
    int Sock;
    int i;

    (void)Protocol;
    if (!T || Domain != AF_INET || Type != SOCK_STREAM) {
        return -1;
    }
    for (i = 0; i < MAX_FDS; i++) {
        if (!T->Fds[i].Used) {
            Slot = i;
            break;
        }
    }
    if (Slot < 0) {
        return -1;
    }
    Sock = LwIpSocketCreate();
    if (Sock < 0) {
        return -1;
    }
    T->Fds[Slot].Used = 1;
    T->Fds[Slot].Kind = FD_KIND_SOCKET;
    T->Fds[Slot].SockId = Sock;
    T->Fds[Slot].Data = 0;
    T->Fds[Slot].Size = 0;
    T->Fds[Slot].Pos = 0;
    T->Fds[Slot].Pages = 0;
    T->Fds[Slot].Path[0] = 0;
    T->Fds[Slot].Dirty = 0;
    return Slot;
}

int SchedulerFdConnect(TASK *T, int Fd, UINT32 Ip, UINT16 Port) {
    if (!T || Fd < 0 || Fd >= MAX_FDS || !T->Fds[Fd].Used) {
        return -1;
    }
    if (T->Fds[Fd].Kind != FD_KIND_SOCKET) {
        return -1;
    }
    return LwIpSocketConnect(T->Fds[Fd].SockId, Ip, Port);
}

int SchedulerFdBind(TASK *T, int Fd, UINT32 Ip, UINT16 Port) {
    if (!T || Fd < 0 || Fd >= MAX_FDS || !T->Fds[Fd].Used) {
        return -1;
    }
    if (T->Fds[Fd].Kind != FD_KIND_SOCKET) {
        return -1;
    }
    return LwIpSocketBind(T->Fds[Fd].SockId, Ip, Port);
}

int SchedulerFdListen(TASK *T, int Fd, int Backlog) {
    if (!T || Fd < 0 || Fd >= MAX_FDS || !T->Fds[Fd].Used) {
        return -1;
    }
    if (T->Fds[Fd].Kind != FD_KIND_SOCKET) {
        return -1;
    }
    return LwIpSocketListen(T->Fds[Fd].SockId, Backlog);
}

int SchedulerFdAccept(TASK *T, int Fd) {
    int Slot = -1;
    int Sock;
    int i;

    if (!T || Fd < 0 || Fd >= MAX_FDS || !T->Fds[Fd].Used) {
        return -1;
    }
    if (T->Fds[Fd].Kind != FD_KIND_SOCKET) {
        return -1;
    }
    for (i = 0; i < MAX_FDS; i++) {
        if (!T->Fds[i].Used) {
            Slot = i;
            break;
        }
    }
    if (Slot < 0) {
        return -1;
    }
    Sock = LwIpSocketAccept(T->Fds[Fd].SockId, 0); /* 0 = 一直等到有连接 */
    if (Sock < 0) {
        return -1;
    }
    T->Fds[Slot].Used = 1;
    T->Fds[Slot].Kind = FD_KIND_SOCKET;
    T->Fds[Slot].SockId = Sock;
    T->Fds[Slot].Data = 0;
    T->Fds[Slot].Size = 0;
    T->Fds[Slot].Pos = 0;
    T->Fds[Slot].Pages = 0;
    T->Fds[Slot].Path[0] = 0;
    T->Fds[Slot].Dirty = 0;
    return Slot;
}

int SchedulerFdRead(TASK *T, int Fd, void *Buf, UINTN Len) {
    TASK_FD *F;
    UINTN N;
    UINTN i;
    int Ret;

    if (!T || !Buf || Fd < 0 || Fd >= MAX_FDS || !T->Fds[Fd].Used) {
        return -1;
    }
    F = &T->Fds[Fd];
    if (F->Kind == FD_KIND_SOCKET) {
        Ret = LwIpSocketRecv(F->SockId, Buf, Len, 2000);
        if (Ret == -2) {
            return 0; /* EOF */
        }
        return Ret;
    }
    if (F->Kind == FD_KIND_PIPE) {
        PIPE *P = PipeFromFd(F);
        if (F->SockId != PIPE_END_READ || !P) {
            return -1;
        }
        if (P->Len == 0) {
            return 0; /* 无数据：无写端则为 EOF；有写端则暂返回 0 */
        }
        N = P->Len;
        if (N > Len) {
            N = Len;
        }
        for (i = 0; i < N; i++) {
            ((UINT8 *)Buf)[i] = P->Buf[P->Head];
            P->Head++;
            if (P->Head >= P->Cap) {
                P->Head = 0;
            }
        }
        P->Len -= N;
        return (int)N;
    }
    if (F->Pos >= F->Size) {
        return 0;
    }
    N = F->Size - F->Pos;
    if (N > Len) {
        N = Len;
    }
    for (i = 0; i < N; i++) {
        ((UINT8 *)Buf)[i] = F->Data[F->Pos + i];
    }
    F->Pos += N;
    return (int)N;
}

int SchedulerFdWrite(TASK *T, int Fd, const void *Buf, UINTN Len) {
    TASK_FD *F;
    UINTN i;

    if (!T || !Buf || Fd < 0 || Fd >= MAX_FDS || !T->Fds[Fd].Used) {
        return -1;
    }
    F = &T->Fds[Fd];
    if (F->Kind == FD_KIND_SOCKET) {
        return LwIpSocketSend(F->SockId, Buf, Len);
    }
    if (F->Kind == FD_KIND_PIPE) {
        PIPE *P = PipeFromFd(F);
        UINTN N;
        if (F->SockId != PIPE_END_WRITE || !P) {
            return -1;
        }
        if (P->Readers <= 0) {
            return -1; /* EPIPE */
        }
        N = P->Cap - P->Len;
        if (N > Len) {
            N = Len;
        }
        for (i = 0; i < N; i++) {
            P->Buf[P->Tail] = ((const UINT8 *)Buf)[i];
            P->Tail++;
            if (P->Tail >= P->Cap) {
                P->Tail = 0;
            }
        }
        P->Len += N;
        return (int)N;
    }
    if (F->Pos > FD_MAX_BYTES) {
        return -1;
    }
    if (F->Pos + Len > FD_MAX_BYTES) {
        Len = FD_MAX_BYTES - F->Pos;
    }
    if (Len == 0) {
        return 0;
    }
    for (i = 0; i < Len; i++) {
        F->Data[F->Pos + i] = ((const UINT8 *)Buf)[i];
    }
    F->Pos += Len;
    if (F->Pos > F->Size) {
        F->Size = F->Pos;
    }
    F->Dirty = 1;
    return (int)Len;
}

int SchedulerFdClose(TASK *T, int Fd) {
    if (!T || Fd < 0 || Fd >= MAX_FDS || !T->Fds[Fd].Used) {
        return -1;
    }
    if (T->Fds[Fd].Kind == FD_KIND_SOCKET) {
        LwIpSocketClose(T->Fds[Fd].SockId);
    } else if (T->Fds[Fd].Kind == FD_KIND_PIPE) {
        PIPE *P = PipeFromFd(&T->Fds[Fd]);
        if (P) {
            if (T->Fds[Fd].SockId == PIPE_END_READ) {
                if (P->Readers > 0) {
                    P->Readers--;
                }
            } else if (P->Writers > 0) {
                P->Writers--;
            }
            if (P->Readers <= 0 && P->Writers <= 0) {
                PhysicalMemoryFreePages(P, P->Pages ? P->Pages : 1);
            }
        }
    } else {
        FdFlush(&T->Fds[Fd]);
        if (T->Fds[Fd].Data) {
            PhysicalMemoryFreePages(T->Fds[Fd].Data, T->Fds[Fd].Pages);
        }
    }
    T->Fds[Fd].Used = 0;
    T->Fds[Fd].Kind = FD_KIND_FILE;
    T->Fds[Fd].SockId = -1;
    T->Fds[Fd].Data = 0;
    T->Fds[Fd].Size = 0;
    T->Fds[Fd].Pos = 0;
    T->Fds[Fd].Pages = 0;
    T->Fds[Fd].Path[0] = 0;
    T->Fds[Fd].Dirty = 0;
    return 0;
}

int SchedulerFdPipe(TASK *T, int PipeFd[2]) {
    int R = -1;
    int W = -1;
    void *Page;
    PIPE *P;
    UINTN Hdr;

    if (!T || !PipeFd) {
        return -1;
    }
    R = FdAllocSlot(T);
    if (R < 0) {
        return -1;
    }
    T->Fds[R].Used = 1; /* 暂占，便于再找写端 */
    W = FdAllocSlot(T);
    if (W < 0) {
        T->Fds[R].Used = 0;
        return -1;
    }

    Page = PhysicalMemoryAllocatePage();
    if (!Page) {
        T->Fds[R].Used = 0;
        return -1;
    }
    {
        UINT8 *B = (UINT8 *)Page;
        UINTN i;
        for (i = 0; i < PAGE_SIZE; i++) {
            B[i] = 0;
        }
    }
    P = (PIPE *)Page;
    Hdr = (sizeof(PIPE) + 15) & ~15ULL;
    if (Hdr >= PAGE_SIZE) {
        PhysicalMemoryFreePage(Page);
        T->Fds[R].Used = 0;
        return -1;
    }
    P->Buf = (UINT8 *)Page + Hdr;
    P->Cap = PAGE_SIZE - Hdr;
    P->Head = 0;
    P->Tail = 0;
    P->Len = 0;
    P->Readers = 1;
    P->Writers = 1;
    P->Pages = 1;

    T->Fds[R].Used = 1;
    T->Fds[R].Kind = FD_KIND_PIPE;
    T->Fds[R].SockId = PIPE_END_READ;
    T->Fds[R].Data = (UINT8 *)(UINTN)P;
    T->Fds[R].Size = 0;
    T->Fds[R].Pos = 0;
    T->Fds[R].Pages = 0;
    T->Fds[R].Path[0] = 0;
    T->Fds[R].Dirty = 0;

    T->Fds[W].Used = 1;
    T->Fds[W].Kind = FD_KIND_PIPE;
    T->Fds[W].SockId = PIPE_END_WRITE;
    T->Fds[W].Data = (UINT8 *)(UINTN)P;
    T->Fds[W].Size = 0;
    T->Fds[W].Pos = 0;
    T->Fds[W].Pages = 0;
    T->Fds[W].Path[0] = 0;
    T->Fds[W].Dirty = 0;

    PipeFd[0] = R;
    PipeFd[1] = W;
    return 0;
}

int SchedulerFdDup(TASK *T, int OldFd) {
    int Slot;
    TASK_FD *S;

    if (!T || OldFd < 0 || OldFd >= MAX_FDS || !T->Fds[OldFd].Used) {
        return -1;
    }
    S = &T->Fds[OldFd];
    if (S->Kind != FD_KIND_PIPE) {
        return -1; /* P2：仅支持 dup 管道端 */
    }
    Slot = FdAllocSlot(T);
    if (Slot < 0) {
        return -1;
    }
    T->Fds[Slot] = *S;
    {
        PIPE *P = PipeFromFd(S);
        if (S->SockId == PIPE_END_READ) {
            P->Readers++;
        } else {
            P->Writers++;
        }
    }
    return Slot;
}

void SchedulerInit(void) {
    int c;

    SpinLockInit(&gSchedLock);
    gSchedOnline = 0;
    gRrHome = 0;
    gStealCount = 0;
    RunqInit();
    for (c = 0; c < HAL_MAX_CPUS; c++) {
        gCurrentCpu[c] = 0;
        gIdleTask[c] = 0;
    }
    for (int i = 0; i < MAX_TASKS; i++) {
        gTasks[i].State = TASK_UNUSED;
        gTasks[i].Frame = 0;
        gTasks[i].Id = (UINT32)i;
        gTasks[i].Ticks = 0;
        gTasks[i].Name[0] = 0;
        gTasks[i].PageRoot = 0;
        gTasks[i].IsUser = 0;
        gTasks[i].Started = 0;
        gTasks[i].UserSpace = 0;
        gTasks[i].ParentId = -1;
        gTasks[i].ExitCode = 0;
        gTasks[i].Waiting = 0;
        gTasks[i].PendingKill = 0;
        gTasks[i].Affinity = -1;
        gTasks[i].OnCpu = -1;
        gTasks[i].HomeCpu = 0;
        gTasks[i].InRunq = 0;
        TaskClearFds(&gTasks[i]);
    }
    gTaskCount = 0;
}

static void CopyName(TASK *T, const char *Name) {
    int n = 0;
    while (Name[n] && n < 15) {
        T->Name[n] = Name[n];
        n++;
    }
    T->Name[n] = 0;
}

static void IdleTask(void) {
    for (;;) {
        HalCpuHalt();
    }
}

int SchedulerCreate(const char *Name, void (*Entry)(void)) {
    SpinLockAcquire(&gSchedLock);
    for (int i = 0; i < MAX_TASKS; i++) {
        if (gTasks[i].State != TASK_UNUSED) {
            continue;
        }
        UINT8 *Top = gTasks[i].Stack + sizeof(gTasks[i].Stack);
        HAL_FRAME *F = (HAL_FRAME *)(Top - sizeof(HAL_FRAME));
        HalFrameSetKernelEntry(F, (UINT64)(UINTN)Entry, (UINT64)(UINTN)Top);

        gTasks[i].Frame = F;
        gTasks[i].State = TASK_READY;
        gTasks[i].Ticks = 0;
        gTasks[i].PageRoot = VirtualMemoryKernelRoot();
        gTasks[i].IsUser = 0;
        gTasks[i].Started = 0;
        gTasks[i].UserSpace = 0;
        gTasks[i].ParentId = -1;
        gTasks[i].ExitCode = 0;
        gTasks[i].Waiting = 0;
        gTasks[i].PendingKill = 0;
        gTasks[i].Affinity = -1;
        gTasks[i].OnCpu = -1;
        gTasks[i].HomeCpu = 0;
        gTasks[i].InRunq = 0;
        TaskClearFds(&gTasks[i]);
        CopyName(&gTasks[i], Name);
        gTaskCount++;
        {
            UINT32 Home = PickHomeCpu(&gTasks[i]);
            RunqEnqueue(Home, &gTasks[i]);
        }
        SpinLockRelease(&gSchedLock);
        return i;
    }
    SpinLockRelease(&gSchedLock);
    return -1;
}

int SchedulerCreateUser(const char *Name, UINT64 Rip, UINT64 Rsp, UINT64 PageRoot,
                    VM_ADDR_SPACE *Space, UINT64 BrkBase) {
    TASK *Cur;

    SpinLockAcquire(&gSchedLock);
    Cur = CurrentTask();
    for (int i = 0; i < MAX_TASKS; i++) {
        if (gTasks[i].State != TASK_UNUSED) {
            continue;
        }
        UINT8 *Top = gTasks[i].Stack + sizeof(gTasks[i].Stack);
        HAL_FRAME *F = (HAL_FRAME *)(Top - sizeof(HAL_FRAME));
        HalFrameSetUserEntry(F, Rip, Rsp);

        gTasks[i].Frame = F;
        gTasks[i].State = TASK_READY;
        gTasks[i].Ticks = 0;
        gTasks[i].PageRoot = PageRoot;
        gTasks[i].IsUser = 1;
        gTasks[i].Started = 0;
        gTasks[i].UserSpace = Space;
        gTasks[i].ParentId = Cur ? TaskSlot(Cur) : -1;
        gTasks[i].ExitCode = 0;
        gTasks[i].Waiting = 0;
        gTasks[i].PendingKill = 0;
        gTasks[i].Affinity = -1; /* PR-S4：每核 TSS，用户态可上 AP */
        gTasks[i].OnCpu = -1;
        gTasks[i].HomeCpu = 0;
        gTasks[i].InRunq = 0;
        gTasks[i].BrkBase = BrkBase;
        gTasks[i].Brk = BrkBase;
        TaskClearFds(&gTasks[i]);
        CopyName(&gTasks[i], Name);
        gTaskCount++;
        {
            UINT32 Home = PickHomeCpu(&gTasks[i]);
            RunqEnqueue(Home, &gTasks[i]);
        }
        SpinLockRelease(&gSchedLock);
        return i;
    }
    SpinLockRelease(&gSchedLock);
    return -1;
}

void SchedulerSetAffinity(int TaskId, INT32 Cpu) {
    if (TaskId < 0 || TaskId >= MAX_TASKS) {
        return;
    }
    SpinLockAcquire(&gSchedLock);
    if (gTasks[TaskId].State != TASK_UNUSED) {
        gTasks[TaskId].Affinity = Cpu;
    }
    SpinLockRelease(&gSchedLock);
}

static int TaskFitsCpu(const TASK *T, UINT32 Cpu) {
    if (!T || T->State != TASK_READY) {
        return 0;
    }
    if (IsIdleTask(T)) {
        return 0;
    }
    if (T->Affinity >= 0 && (UINT32)T->Affinity != Cpu) {
        return 0;
    }
    return 1;
}

static TASK *PickNext(UINT32 Cpu) {
    TASK *Idle = (Cpu < HAL_MAX_CPUS) ? gIdleTask[Cpu] : 0;
    TASK *T;
    int Cpus;
    int v;

    /* 1) 本核队列 */
    for (;;) {
        T = RunqDequeue(Cpu);
        if (!T) {
            break;
        }
        if (TaskFitsCpu(T, Cpu)) {
            return T;
        }
        /* 亲和性不符：送回其 Home / Affinity */
        RunqEnqueue(PickHomeCpu(T), T);
    }

    /* 2) 从其它核偷 */
    Cpus = HalCpuCount();
    if (Cpus < 1) {
        Cpus = 1;
    }
    if (Cpus > HAL_MAX_CPUS) {
        Cpus = HAL_MAX_CPUS;
    }
    for (v = 1; v < Cpus; v++) {
        UINT32 Vic = (Cpu + (UINT32)v) % (UINT32)Cpus;
        T = RunqStealOne(Vic);
        if (!T) {
            continue;
        }
        if (TaskFitsCpu(T, Cpu)) {
            gStealCount++;
            return T;
        }
        RunqEnqueue(PickHomeCpu(T), T);
    }

    if (Idle && Idle->State != TASK_UNUSED) {
        if (Idle->State == TASK_RUNNING || Idle->State == TASK_READY) {
            return Idle;
        }
        Idle->State = TASK_READY;
        return Idle;
    }
    return CurrentTask();
}

static UINT64 SchedResumeFrame(TASK *T) {
    UINT64 Frame = (UINT64)(UINTN)T->Frame;

    if (Frame == 0) {
        return 0;
    }
    if (T->Started) {
        return Frame;
    }
    T->Started = 1;
    if (T->IsUser) {
        return Frame | SCHED_TAG_USER_FIRST;
    }
    return Frame | SCHED_TAG_KERNEL_FIRST;
}

/* Ring3 中断/系统调用走 TSS.RSP0；每用户任务必须用自己的内核栈 */
static void ActivateTask(TASK *T) {
    UINT32 Cpu = HalCpuId();
    TASK *Prev = CurrentTask();

    if (Prev && Prev != T && Prev->State == TASK_RUNNING) {
        Prev->State = TASK_READY;
        Prev->OnCpu = -1;
        if (!IsIdleTask(Prev)) {
            RunqEnqueue(Cpu, Prev); /* 留在本核队列，利于缓存 */
        }
    }
    RunqRemove(T);
    SetCurrentTask(T);
    T->State = TASK_RUNNING;
    T->OnCpu = (INT32)Cpu;
    if (T->IsUser) {
        HalSetKernelStack((UINT64)(UINTN)(T->Stack + sizeof(T->Stack)));
    }
    if (T->PageRoot != 0) {
        VirtualMemoryLoadPageTable(T->PageRoot);
    }
}

static TASK *FindRunnable(UINT32 Cpu) {
    return PickNext(Cpu);
}

static void ReapZombie(TASK *Z) {
    RunqRemove(Z);
    SchedulerFdCloseAll(Z);
    if (Z->UserSpace) {
        VirtualMemorySpaceDestroy(Z->UserSpace);
        Z->UserSpace = 0;
    }
    Z->State = TASK_UNUSED;
    Z->Frame = 0;
    Z->PageRoot = 0;
    Z->IsUser = 0;
    Z->Started = 0;
    Z->ParentId = -1;
    Z->Waiting = 0;
    Z->PendingKill = 0;
    Z->OnCpu = -1;
    Z->InRunq = 0;
    gTaskCount--;
}

/* 仅用户父进程会 wait()；shell/内核为父时直接回收，避免僵尸占满任务槽 */
static int ParentIsUserWaiter(INT32 ParentSlot) {
    TASK *P;

    if (ParentSlot < 0 || ParentSlot >= MAX_TASKS) {
        return 0;
    }
    P = &gTasks[ParentSlot];
    if (P->State == TASK_UNUSED) {
        return 0;
    }
    return P->IsUser;
}

void SchedulerReapOrphanZombies(void) {
    int i;

    SpinLockAcquire(&gSchedLock);
    for (i = 0; i < MAX_TASKS; i++) {
        if (gTasks[i].State != TASK_ZOMBIE || !gTasks[i].IsUser) {
            continue;
        }
        if (!ParentIsUserWaiter(gTasks[i].ParentId)) {
            ReapZombie(&gTasks[i]);
        }
    }
    SpinLockRelease(&gSchedLock);
}

/* 若父进程正阻塞在 wait：把僵尸结果写入其 Frame 并唤醒，返回 1 表示已收尸 */
static int WakeWaitingParent(TASK *Zombie) {
    TASK *P;
    INT32 ParentId;

    if (!Zombie) {
        return 0;
    }
    ParentId = Zombie->ParentId;
    if (ParentId < 0 || ParentId >= MAX_TASKS) {
        return 0;
    }
    P = &gTasks[ParentId];
    if (P->State != TASK_BLOCKED || !P->Waiting) {
        return 0;
    }
    if (P->Frame) {
        HalFrameSetReturn2(P->Frame,
                           (UINT64)(UINT32)TaskSlot(Zombie),
                           (UINT64)(UINT32)Zombie->ExitCode);
    }
    P->Waiting = 0;
    P->State = TASK_READY;
    RunqEnqueue(PickHomeCpu(P), P);
    ReapZombie(Zombie);
    return 1;
}

/*
 * 持锁：结束用户任务（exit / kill 共用）。
 * *ShowPrompt：无用户父、立即回收时置 1。
 * 返回 1：目标是当前任务，调用方须切走；0：目标非当前。
 */
static int TerminateUserLocked(TASK *Exiting, INT32 Code, int *ShowPrompt) {
    if (!Exiting || !Exiting->IsUser) {
        return 0;
    }
    if (ShowPrompt) {
        *ShowPrompt = 0;
    }

    SchedulerFdCloseAll(Exiting);
    if (Exiting->UserSpace) {
        VirtualMemorySpaceDestroy(Exiting->UserSpace);
        Exiting->UserSpace = 0;
    }
    Exiting->ExitCode = Code;
    Exiting->PageRoot = VirtualMemoryKernelRoot();
    Exiting->Waiting = 0;
    Exiting->PendingKill = 0;
    Exiting->OnCpu = -1;
    RunqRemove(Exiting);

    if (ParentIsUserWaiter(Exiting->ParentId)) {
        Exiting->State = TASK_ZOMBIE;
        if (!WakeWaitingParent(Exiting)) {
            /* 父用户进程稍后 wait */
        }
    } else {
        if (ShowPrompt) {
            *ShowPrompt = 1;
        }
        Exiting->State = TASK_UNUSED;
        Exiting->Frame = 0;
        Exiting->ParentId = -1;
        gTaskCount--;
    }

    return Exiting == CurrentTask() ? 1 : 0;
}

static int SignalDefaultTerminates(INT32 Sig) {
    return Sig == SIGKILL || Sig == SIGTERM || Sig == SIGINT;
}

/* 持锁：对用户任务投递默认终止。返回：0 成功且勿切；1 成功且须切走；-1 失败 */
static int DeliverKillLocked(TASK *T, INT32 Sig, int *ShowPrompt) {
    INT32 Code;
    UINT32 CurCpu;

    if (!T || !T->IsUser || !SignalDefaultTerminates(Sig)) {
        return -1;
    }
    if (T->State == TASK_UNUSED || T->State == TASK_ZOMBIE) {
        return -1;
    }

    Code = 128 + Sig;
    CurCpu = HalCpuId();

    /* 他核 RUNNING：挂起，待该核 timer/syscall 入口完成终止（避免拆用户页表竞态） */
    if (T->State == TASK_RUNNING && T->OnCpu >= 0 &&
        (UINT32)T->OnCpu != CurCpu && T != CurrentTask()) {
        T->PendingKill = Sig;
        return 0;
    }

    return TerminateUserLocked(T, Code, ShowPrompt);
}

UINT64 SchedulerOnTimer(HAL_FRAME *Frame) {
    TASK *Cur;
    TASK *Next;
    UINT32 Cpu;
    UINT64 Ret;
    int ShowPrompt = 0;

    if (!gSchedOnline) {
        return 0;
    }
    Cpu = HalCpuId();
    SpinLockAcquire(&gSchedLock);
    Cur = CurrentTask();
    if (Cur == 0) {
        SpinLockRelease(&gSchedLock);
        return 0;
    }
    Cur->Frame = Frame;
    Cur->Ticks++;

    if (Cur->IsUser && Cur->PendingKill > 0) {
        INT32 Sig = Cur->PendingKill;
        Cur->PendingKill = 0;
        if (TerminateUserLocked(Cur, 128 + Sig, &ShowPrompt)) {
            Next = FindRunnable(Cpu);
            if (!Next) {
                SpinLockRelease(&gSchedLock);
                ConsoleWrite("sched: no runnable after pending kill\n");
                for (;;) {
                    HalCpuPark();
                }
            }
            ActivateTask(Next);
            Ret = SchedResumeFrame(Next);
            SpinLockRelease(&gSchedLock);
            if (ShowPrompt) {
                ConsoleShowPrompt();
            }
            return Ret;
        }
    }

    Next = PickNext(Cpu);
    if (Next == Cur || Next == 0) {
        SpinLockRelease(&gSchedLock);
        return 0;
    }
    ActivateTask(Next);
    Ret = SchedResumeFrame(Next);
    SpinLockRelease(&gSchedLock);
    return Ret;
}

static int gCoopDrain;

void SchedulerCoopDrainUsers(void) {
    TASK *Host;
    UINT32 Cpu;
    int i;

    if (!HalPlatformVirtConsole()) {
        return;
    }

    Host = CurrentTask();
    if (!Host || Host->IsUser) {
        return;
    }

    gCoopDrain = 1;
    Cpu = HalCpuId();

    for (;;) {
        TASK *U = 0;
        UINT64 Ksp;

        SpinLockAcquire(&gSchedLock);
        for (i = 0; i < MAX_TASKS; i++) {
            if (gTasks[i].State == TASK_READY && gTasks[i].IsUser &&
                gTasks[i].Frame != 0) {
                U = &gTasks[i];
                break;
            }
        }
        if (!U) {
            SpinLockRelease(&gSchedLock);
            break;
        }
        ActivateTask(U);
        U->Started = 1;
        Ksp = (UINT64)(UINTN)(U->Stack + sizeof(U->Stack));
        SpinLockRelease(&gSchedLock);

        HalSetKernelStack(Ksp);
        HalUserCoopEnter(Ksp, U->Frame);

        /* exit → HalUserCoopReturn；恢复宿主内核任务 */
        SpinLockAcquire(&gSchedLock);
        ActivateTask(Host);
        Host->State = TASK_RUNNING;
        Host->OnCpu = (INT32)Cpu;
        SpinLockRelease(&gSchedLock);
        VirtualMemoryLoadPageTable(VirtualMemoryKernelRoot());
    }

    gCoopDrain = 0;
}

UINT64 SchedulerExitUser(HAL_FRAME *Frame) {
    INT32 Code;
    TASK *Exiting;
    TASK *Next;
    UINT32 Cpu;
    UINT64 Ret;
    int ShowPrompt = 0;

    SpinLockAcquire(&gSchedLock);
    Exiting = CurrentTask();
    if (Exiting == 0 || !Exiting->IsUser) {
        SpinLockRelease(&gSchedLock);
        for (;;) {
            HalCpuPark();
        }
    }

    Code = (INT32)HalFrameArg0(Frame);
    DebugWrite("syscall: exit ");
    DebugWrite(Exiting->Name);
    DebugWrite(" code=");
    DebugHex32((UINT32)Code);
    DebugWrite("\n");

    (void)TerminateUserLocked(Exiting, Code, &ShowPrompt);

    if (gCoopDrain) {
        SpinLockRelease(&gSchedLock);
        if (ShowPrompt) {
            ConsoleShowPrompt();
        }
        VirtualMemoryLoadPageTable(VirtualMemoryKernelRoot());
        HalUserCoopReturn();
        return 0;
    }

    Cpu = HalCpuId();
    Next = FindRunnable(Cpu);
    if (!Next) {
        SpinLockRelease(&gSchedLock);
        ConsoleWrite("sched: no runnable task after exit\n");
        for (;;) {
            HalCpuPark();
        }
    }
    ActivateTask(Next);
    Ret = SchedResumeFrame(Next);
    SpinLockRelease(&gSchedLock);
    if (ShowPrompt) {
        ConsoleShowPrompt();
    }
    return Ret;
}

UINT64 SchedulerFork(HAL_FRAME *Frame) {
    VM_ADDR_SPACE *ChildSpace;
    TASK *Parent;
    INT32 ParentSlot;
    int Child;
    UINT8 *Top;
    HAL_FRAME *CF;

    SpinLockAcquire(&gSchedLock);
    Parent = CurrentTask();
    ParentSlot = TaskSlot(Parent);
    if (Parent == 0 || ParentSlot < 0 || !Parent->IsUser || !Parent->UserSpace) {
        HalFrameSetReturn(Frame, (UINT64)(INT64)-1);
        SpinLockRelease(&gSchedLock);
        return 0;
    }

    Child = -1;
    for (int i = 0; i < MAX_TASKS; i++) {
        if (gTasks[i].State == TASK_UNUSED) {
            Child = i;
            break;
        }
    }
    if (Child < 0) {
        HalFrameSetReturn(Frame, (UINT64)(INT64)-1);
        SpinLockRelease(&gSchedLock);
        return 0;
    }

    HalFrameSetReturn(Frame, (UINT64)(UINT32)(Child + 1));
    SpinLockRelease(&gSchedLock);

    ChildSpace = VirtualMemorySpaceClone(Parent->UserSpace);
    if (!ChildSpace) {
        HalFrameSetReturn(Frame, (UINT64)(INT64)-1);
        return 0;
    }

    SpinLockAcquire(&gSchedLock);
    /* 槽位仍应空闲；若竞态被占则放弃 */
    if (gTasks[Child].State != TASK_UNUSED) {
        SpinLockRelease(&gSchedLock);
        VirtualMemorySpaceDestroy(ChildSpace);
        HalFrameSetReturn(Frame, (UINT64)(INT64)-1);
        return 0;
    }

    Top = gTasks[Child].Stack + sizeof(gTasks[Child].Stack);
    CF = (HAL_FRAME *)(Top - sizeof(HAL_FRAME));
    HalFrameCopy(CF, Frame);
    HalFrameSetReturn(CF, 0);

    gTasks[Child].Frame = CF;
    gTasks[Child].State = TASK_READY;
    gTasks[Child].Ticks = 0;
    gTasks[Child].PageRoot = VirtualMemorySpaceRoot(ChildSpace);
    gTasks[Child].IsUser = 1;
    gTasks[Child].Started = 0;
    gTasks[Child].UserSpace = ChildSpace;
    gTasks[Child].ParentId = ParentSlot;
    gTasks[Child].ExitCode = 0;
    gTasks[Child].Waiting = 0;
    gTasks[Child].PendingKill = 0;
    gTasks[Child].Affinity = -1;
    gTasks[Child].OnCpu = -1;
    gTasks[Child].HomeCpu = 0;
    gTasks[Child].InRunq = 0;
    gTasks[Child].BrkBase = Parent->BrkBase;
    gTasks[Child].Brk = Parent->Brk;
    TaskCloneFds(&gTasks[Child], Parent);
    CopyName(&gTasks[Child], Parent->Name);
    gTaskCount++;
    RunqEnqueue(PickHomeCpu(&gTasks[Child]), &gTasks[Child]);

    HalFrameSetReturn(Frame, (UINT64)(UINT32)(Child + 1));
    Parent->Frame = Frame;
    SpinLockRelease(&gSchedLock);
    return 0;
}

UINT64 SchedulerWait(HAL_FRAME *Frame) {
    TASK *Self;
    INT32 MyId;
    int i;
    int Live = 0;
    TASK *Next;
    UINT64 Options;
    UINT32 Cpu;
    UINT64 Ret;

    SpinLockAcquire(&gSchedLock);
    Self = CurrentTask();
    if (Self == 0 || !Self->IsUser) {
        HalFrameSetReturn(Frame, (UINT64)(INT64)-1);
        SpinLockRelease(&gSchedLock);
        return 0;
    }
    MyId = TaskSlot(Self);
    Options = HalFrameArg0(Frame);

    for (i = 0; i < MAX_TASKS; i++) {
        if (gTasks[i].State == TASK_ZOMBIE && gTasks[i].ParentId == MyId) {
            INT32 Code = gTasks[i].ExitCode;
            INT32 Cid = TaskSlot(&gTasks[i]);
            ReapZombie(&gTasks[i]);
            HalFrameSetReturn2(Frame, (UINT64)(UINT32)Cid, (UINT64)(UINT32)Code);
            SpinLockRelease(&gSchedLock);
            return 0;
        }
    }
    for (i = 0; i < MAX_TASKS; i++) {
        if (gTasks[i].ParentId == MyId &&
            (gTasks[i].State == TASK_READY ||
             gTasks[i].State == TASK_RUNNING ||
             gTasks[i].State == TASK_BLOCKED)) {
            Live = 1;
            break;
        }
    }
    if (!Live) {
        HalFrameSetReturn(Frame, (UINT64)(INT64)-1);
        SpinLockRelease(&gSchedLock);
        return 0;
    }

    if (Options & (UINT64)WNOHANG) {
        HalFrameSetReturn2(Frame, 0, 0);
        SpinLockRelease(&gSchedLock);
        return 0;
    }

    Self->Frame = Frame;
    Self->Waiting = 1;
    Self->State = TASK_BLOCKED;
    Self->OnCpu = -1;

    Cpu = HalCpuId();
    Next = FindRunnable(Cpu);
    if (!Next) {
        SpinLockRelease(&gSchedLock);
        ConsoleWrite("sched: wait with no runnable task\n");
        for (;;) {
            HalCpuPark();
        }
    }
    ActivateTask(Next);
    Ret = SchedResumeFrame(Next);
    SpinLockRelease(&gSchedLock);
    return Ret;
}

UINT64 SchedulerKill(HAL_FRAME *Frame) {
    INT32 Pid;
    INT32 Sig;
    INT32 Slot;
    TASK *T;
    TASK *Next;
    UINT32 Cpu;
    UINT64 Ret;
    int ShowPrompt = 0;
    int Deliver;

    SpinLockAcquire(&gSchedLock);
    Pid = (INT32)HalFrameArg0(Frame);
    Sig = (INT32)HalFrameArg1(Frame);
    if (Pid <= 0 || !SignalDefaultTerminates(Sig)) {
        HalFrameSetReturn(Frame, (UINT64)(INT64)-1);
        SpinLockRelease(&gSchedLock);
        return 0;
    }
    Slot = Pid - 1;
    if (Slot < 0 || Slot >= MAX_TASKS) {
        HalFrameSetReturn(Frame, (UINT64)(INT64)-1);
        SpinLockRelease(&gSchedLock);
        return 0;
    }
    T = &gTasks[Slot];
    Deliver = DeliverKillLocked(T, Sig, &ShowPrompt);
    if (Deliver < 0) {
        HalFrameSetReturn(Frame, (UINT64)(INT64)-1);
        SpinLockRelease(&gSchedLock);
        return 0;
    }
    if (Deliver == 0) {
        HalFrameSetReturn(Frame, 0);
        SpinLockRelease(&gSchedLock);
        return 0;
    }

    /* 杀自身：切到其他可运行任务 */
    Cpu = HalCpuId();
    Next = FindRunnable(Cpu);
    if (!Next) {
        SpinLockRelease(&gSchedLock);
        ConsoleWrite("sched: no runnable after self-kill\n");
        for (;;) {
            HalCpuPark();
        }
    }
    ActivateTask(Next);
    Ret = SchedResumeFrame(Next);
    SpinLockRelease(&gSchedLock);
    if (ShowPrompt) {
        ConsoleShowPrompt();
    }
    return Ret;
}

int SchedulerKillPid(INT32 Pid, INT32 Sig) {
    INT32 Slot;
    TASK *T;
    int ShowPrompt = 0;
    int Deliver;
    TASK *Cur;
    TASK *Next;
    UINT32 Cpu;
    UINT64 Ret;

    if (Pid <= 0 || !SignalDefaultTerminates(Sig)) {
        return -1;
    }
    Slot = Pid - 1;
    if (Slot < 0 || Slot >= MAX_TASKS) {
        return -1;
    }

    SpinLockAcquire(&gSchedLock);
    T = &gTasks[Slot];
    Cur = CurrentTask();
    Deliver = DeliverKillLocked(T, Sig, &ShowPrompt);
    if (Deliver < 0) {
        SpinLockRelease(&gSchedLock);
        return -1;
    }
    if (Deliver == 0 || T != Cur) {
        SpinLockRelease(&gSchedLock);
        if (ShowPrompt) {
            ConsoleShowPrompt();
        }
        return 0;
    }

    Cpu = HalCpuId();
    Next = FindRunnable(Cpu);
    if (!Next) {
        SpinLockRelease(&gSchedLock);
        return -1;
    }
    ActivateTask(Next);
    Ret = SchedResumeFrame(Next);
    (void)Ret;
    SpinLockRelease(&gSchedLock);
    if (ShowPrompt) {
        ConsoleShowPrompt();
    }
    return 0;
}

UINT64 SchedulerYield(HAL_FRAME *Frame) {
    TASK *Cur;
    TASK *Next;
    UINT32 Cpu;
    UINT64 Ret;

    SpinLockAcquire(&gSchedLock);
    Cur = CurrentTask();
    if (Cur == 0) {
        SpinLockRelease(&gSchedLock);
        return 0;
    }
    Cur->Frame = Frame;
    Cpu = HalCpuId();
    Next = PickNext(Cpu);
    if (Next == Cur || Next == 0) {
        SpinLockRelease(&gSchedLock);
        return 0;
    }
    ActivateTask(Next);
    Ret = SchedResumeFrame(Next);
    SpinLockRelease(&gSchedLock);
    return Ret;
}

static int CreateIdleForCpu(UINT32 Cpu) {
    char Name[12];
    int Id;

    Name[0] = 'i';
    Name[1] = 'd';
    Name[2] = 'l';
    Name[3] = 'e';
    Name[4] = (char)('0' + (Cpu % 10));
    Name[5] = 0;
    Id = SchedulerCreate(Name, IdleTask);
    if (Id < 0) {
        return -1;
    }
    SchedulerSetAffinity(Id, (INT32)Cpu);
    SpinLockAcquire(&gSchedLock);
    gIdleTask[Cpu] = &gTasks[Id];
    RunqRemove(gIdleTask[Cpu]);
    SpinLockRelease(&gSchedLock);
    return Id;
}

int SchedulerIsOnline(void) {
    return gSchedOnline;
}

void SchedulerApStart(void) {
    UINT32 Cpu;
    TASK *Idle;
    UINT64 Ret;

    Cpu = HalCpuId();
    while (!gSchedOnline) {
        HalCpuRelax();
    }
    SpinLockAcquire(&gSchedLock);
    Idle = (Cpu < HAL_MAX_CPUS) ? gIdleTask[Cpu] : 0;
    if (!Idle) {
        SpinLockRelease(&gSchedLock);
        HalDebugWrite("sched: AP has no idle\n");
        for (;;) {
            HalCpuPark();
        }
    }
    ActivateTask(Idle);
    Idle->Started = 1;
    Ret = (UINT64)(UINTN)Idle->Frame;
    SpinLockRelease(&gSchedLock);
    HalDebugWrite("sched: AP entered idle cpu=");
    HalDebugHex32(Cpu);
    HalDebugWrite("\n");
    HalSchedulerEnter(Idle->Frame);
    (void)Ret;
    for (;;) {
        HalCpuPark();
    }
}

void SchedulerStart(void) {
    TASK *First = 0;
    int Cpus;
    int c;
    int i;

    Cpus = HalCpuCount();
    if (Cpus < 1) {
        Cpus = 1;
    }
    if (Cpus > HAL_MAX_CPUS) {
        Cpus = HAL_MAX_CPUS;
    }
    for (c = 0; c < Cpus; c++) {
        if (CreateIdleForCpu((UINT32)c) < 0) {
            ConsoleWrite("sched: idle create failed\n");
            for (;;) {
                HalCpuPark();
            }
        }
    }

    /*
     * shell/gui 仍绑 BSP（Console/帧缓冲无大锁）；
     * worker / 用户态 Affinity=-1，靠每核队列 + 偷任务上 AP。
     */
    for (i = 0; i < MAX_TASKS; i++) {
        if (gTasks[i].State == TASK_UNUSED) {
            continue;
        }
        if ((gTasks[i].Name[0] == 's' && gTasks[i].Name[1] == 'h') ||
            (gTasks[i].Name[0] == 'g' && gTasks[i].Name[1] == 'u')) {
            RunqRemove(&gTasks[i]);
            gTasks[i].Affinity = 0;
            RunqEnqueue(0, &gTasks[i]);
        }
    }

    for (i = 0; i < MAX_TASKS; i++) {
        if (gTasks[i].State != TASK_READY) {
            continue;
        }
        if (gIdleTask[0] && &gTasks[i] == gIdleTask[0]) {
            continue;
        }
        First = &gTasks[i];
        break;
    }
    if (!First) {
        ConsoleWrite("sched: no tasks\n");
        for (;;) {
            HalCpuPark();
        }
    }

    HalIrqDisable();
    SpinLockAcquire(&gSchedLock);
    ActivateTask(First);
    First->Started = 1;
    gSchedOnline = 1;
    SpinLockRelease(&gSchedLock);
    HalTimerStart();
    DebugWrite("sched: online, entering tasks\n");
    HalSchedulerEnter(First->Frame);
}

TASK *SchedulerCurrent(void) {
    return CurrentTask();
}

int SchedulerTaskCount(void) {
    return gTaskCount;
}

UINT64 SchedulerStealCount(void) {
    return gStealCount;
}

const TASK *SchedulerTaskByIndex(int Index) {
    if (Index < 0 || Index >= MAX_TASKS) {
        return 0;
    }
    if (gTasks[Index].State == TASK_UNUSED) {
        return 0;
    }
    return &gTasks[Index];
}

UINT64 SchedulerTaskRip(const TASK *T) {
    if (!T || !T->Frame) {
        return 0;
    }
    return HalFrameGetRip(T->Frame);
}
