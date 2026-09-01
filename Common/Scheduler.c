/*
 * Scheduler.c — 抢占式任务调度器（含 fork/wait 与每任务 FD）
 */
#include "Scheduler.h"
#include "Fat.h"
#include "hal.h"
#include "Console.h"
#include "Debug.h"
#include "VirtualMemory.h"
#include "PhysicalMemory.h"

#define SCHED_TAG_KERNEL_FIRST 1ULL
#define SCHED_TAG_USER_FIRST   2ULL

static TASK gTasks[MAX_TASKS];
static TASK *gCurrent;
static int gTaskCount;
static int gLastPick;

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
        T->Fds[i].Data = 0;
        T->Fds[i].Size = 0;
        T->Fds[i].Pos = 0;
        T->Fds[i].Pages = 0;
    }
}

void SchedulerFdCloseAll(TASK *T) {
    int i;
    if (!T) {
        return;
    }
    for (i = 0; i < MAX_FDS; i++) {
        if (T->Fds[i].Used && T->Fds[i].Data) {
            PhysicalMemoryFreePages(T->Fds[i].Data, T->Fds[i].Pages);
        }
        T->Fds[i].Used = 0;
        T->Fds[i].Data = 0;
        T->Fds[i].Size = 0;
        T->Fds[i].Pos = 0;
        T->Fds[i].Pages = 0;
    }
}

int SchedulerFdOpen(TASK *T, const char *Path) {
    int Slot = -1;
    UINT32 Pages;
    void *Buf;
    UINTN Size = 0;
    int i;

    if (!T || !Path) {
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
    Pages = (FD_MAX_BYTES + PAGE_SIZE - 1) / PAGE_SIZE;
    Buf = PhysicalMemoryAllocatePages(Pages);
    if (!Buf) {
        return -1;
    }
    if (!FatReadFile(Path, Buf, FD_MAX_BYTES, &Size) || Size == 0) {
        PhysicalMemoryFreePages(Buf, Pages);
        return -1;
    }
    T->Fds[Slot].Used = 1;
    T->Fds[Slot].Data = (UINT8 *)Buf;
    T->Fds[Slot].Size = Size;
    T->Fds[Slot].Pos = 0;
    T->Fds[Slot].Pages = Pages;
    return Slot;
}

int SchedulerFdRead(TASK *T, int Fd, void *Buf, UINTN Len) {
    TASK_FD *F;
    UINTN N;
    UINTN i;

    if (!T || !Buf || Fd < 0 || Fd >= MAX_FDS || !T->Fds[Fd].Used) {
        return -1;
    }
    F = &T->Fds[Fd];
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

int SchedulerFdClose(TASK *T, int Fd) {
    if (!T || Fd < 0 || Fd >= MAX_FDS || !T->Fds[Fd].Used) {
        return -1;
    }
    if (T->Fds[Fd].Data) {
        PhysicalMemoryFreePages(T->Fds[Fd].Data, T->Fds[Fd].Pages);
    }
    T->Fds[Fd].Used = 0;
    T->Fds[Fd].Data = 0;
    T->Fds[Fd].Size = 0;
    T->Fds[Fd].Pos = 0;
    T->Fds[Fd].Pages = 0;
    return 0;
}

void SchedulerInit(void) {
    for (int i = 0; i < MAX_TASKS; i++) {
        gTasks[i].State = TASK_UNUSED;
        gTasks[i].Frame = 0;
        gTasks[i].Id = (UINT32)i;
        gTasks[i].Ticks = 0;
        gTasks[i].Name[0] = 0;
        gTasks[i].Cr3 = 0;
        gTasks[i].IsUser = 0;
        gTasks[i].Started = 0;
        gTasks[i].UserSpace = 0;
        gTasks[i].ParentId = -1;
        gTasks[i].ExitCode = 0;
        gTasks[i].Waiting = 0;
        TaskClearFds(&gTasks[i]);
    }
    gCurrent = 0;
    gTaskCount = 0;
    gLastPick = 0;
}

static void CopyName(TASK *T, const char *Name) {
    int n = 0;
    while (Name[n] && n < 15) {
        T->Name[n] = Name[n];
        n++;
    }
    T->Name[n] = 0;
}

int SchedulerCreate(const char *Name, void (*Entry)(void)) {
    for (int i = 0; i < MAX_TASKS; i++) {
        if (gTasks[i].State != TASK_UNUSED) {
            continue;
        }
        UINT8 *Top = gTasks[i].Stack + sizeof(gTasks[i].Stack);
        struct INT_FRAME *F = (struct INT_FRAME *)(Top - sizeof(struct INT_FRAME));
        for (UINTN j = 0; j < sizeof(struct INT_FRAME); j++) {
            ((UINT8 *)F)[j] = 0;
        }
        F->Rip = (UINT64)(UINTN)Entry;
        F->Cs = 0x08;
        F->Rflags = 0x202;
        F->Rsp = (UINT64)(UINTN)Top;
        F->Ss = 0x10;
        F->Vector = VEC_TIMER;
        F->ErrorCode = 0;

        gTasks[i].Frame = F;
        gTasks[i].State = TASK_READY;
        gTasks[i].Ticks = 0;
        gTasks[i].Cr3 = VirtualMemoryKernelCr3();
        gTasks[i].IsUser = 0;
        gTasks[i].Started = 0;
        gTasks[i].UserSpace = 0;
        gTasks[i].ParentId = -1;
        gTasks[i].ExitCode = 0;
        gTasks[i].Waiting = 0;
        TaskClearFds(&gTasks[i]);
        CopyName(&gTasks[i], Name);
        gTaskCount++;
        return i;
    }
    return -1;
}

int SchedulerCreateUser(const char *Name, UINT64 Rip, UINT64 Rsp, UINT64 Cr3,
                    VM_ADDR_SPACE *Space) {
    for (int i = 0; i < MAX_TASKS; i++) {
        if (gTasks[i].State != TASK_UNUSED) {
            continue;
        }
        UINT8 *Top = gTasks[i].Stack + sizeof(gTasks[i].Stack);
        struct INT_FRAME *F = (struct INT_FRAME *)(Top - sizeof(struct INT_FRAME));
        for (UINTN j = 0; j < sizeof(struct INT_FRAME); j++) {
            ((UINT8 *)F)[j] = 0;
        }
        F->Rip = Rip;
        F->Cs = 0x23;
        F->Rflags = 0x202;
        F->Rsp = Rsp;
        F->Ss = 0x1B;
        F->Vector = VEC_TIMER;
        F->ErrorCode = 0;

        gTasks[i].Frame = F;
        gTasks[i].State = TASK_READY;
        gTasks[i].Ticks = 0;
        gTasks[i].Cr3 = Cr3;
        gTasks[i].IsUser = 1;
        gTasks[i].Started = 0;
        gTasks[i].UserSpace = Space;
        gTasks[i].ParentId = gCurrent ? TaskSlot(gCurrent) : -1;
        gTasks[i].ExitCode = 0;
        gTasks[i].Waiting = 0;
        TaskClearFds(&gTasks[i]);
        CopyName(&gTasks[i], Name);
        gTaskCount++;
        return i;
    }
    return -1;
}

static int TaskRunnable(TASK_STATE S) {
    return S == TASK_READY || S == TASK_RUNNING;
}

static TASK *PickNext(void) {
    if (gTaskCount <= 1) {
        return gCurrent;
    }
    for (int n = 1; n <= MAX_TASKS; n++) {
        int i = (gLastPick + n) % MAX_TASKS;
        if (TaskRunnable(gTasks[i].State)) {
            gLastPick = i;
            return &gTasks[i];
        }
    }
    return gCurrent;
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
    gCurrent = T;
    T->State = TASK_RUNNING;
    if (T->IsUser) {
        ArchSetRsp0((UINT64)(UINTN)(T->Stack + sizeof(T->Stack)));
    }
    if (T->Cr3 != 0) {
        VirtualMemoryLoadCr3(T->Cr3);
    }
}

static TASK *FindRunnable(void) {
    for (int n = 1; n <= MAX_TASKS; n++) {
        int i = (gLastPick + n) % MAX_TASKS;
        if (TaskRunnable(gTasks[i].State)) {
            gLastPick = i;
            return &gTasks[i];
        }
    }
    for (int i = 0; i < MAX_TASKS; i++) {
        if (TaskRunnable(gTasks[i].State)) {
            gLastPick = i;
            return &gTasks[i];
        }
    }
    return 0;
}

static void ReapZombie(TASK *Z) {
    SchedulerFdCloseAll(Z);
    if (Z->UserSpace) {
        VirtualMemorySpaceDestroy(Z->UserSpace);
        Z->UserSpace = 0;
    }
    Z->State = TASK_UNUSED;
    Z->Frame = 0;
    Z->Cr3 = 0;
    Z->IsUser = 0;
    Z->Started = 0;
    Z->ParentId = -1;
    Z->Waiting = 0;
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

    for (i = 0; i < MAX_TASKS; i++) {
        if (gTasks[i].State != TASK_ZOMBIE || !gTasks[i].IsUser) {
            continue;
        }
        if (!ParentIsUserWaiter(gTasks[i].ParentId)) {
            ReapZombie(&gTasks[i]);
        }
    }
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
        P->Frame->Rax = (UINT64)(UINT32)TaskSlot(Zombie);
        P->Frame->Rdx = (UINT64)(UINT32)Zombie->ExitCode;
    }
    P->Waiting = 0;
    P->State = TASK_READY;
    ReapZombie(Zombie);
    return 1;
}

UINT64 SchedulerOnTimer(struct INT_FRAME *Frame) {
    if (gCurrent == 0) {
        return 0;
    }
    gCurrent->Frame = Frame;
    gCurrent->Ticks++;

    TASK *Next = PickNext();
    if (Next == gCurrent) {
        return 0;
    }
    if (gCurrent->State == TASK_RUNNING) {
        gCurrent->State = TASK_READY;
    }
    ActivateTask(Next);
    return SchedResumeFrame(gCurrent);
}

UINT64 SchedulerExitUser(struct INT_FRAME *Frame) {
    INT32 Code;
    TASK *Exiting;
    TASK *Next;

    if (gCurrent == 0 || !gCurrent->IsUser) {
        for (;;) {
            HalCpuPark();
        }
    }

    Exiting = gCurrent;
    Code = (INT32)Frame->Rdi;
    ConsoleWrite("syscall: exit ");
    ConsoleWrite(Exiting->Name);
    ConsoleWrite(" code=");
    ConsoleHex32((UINT32)Code);
    ConsoleWrite("\n");

    SchedulerFdCloseAll(Exiting);
    if (Exiting->UserSpace) {
        VirtualMemorySpaceDestroy(Exiting->UserSpace);
        Exiting->UserSpace = 0;
    }
    Exiting->ExitCode = Code;
    Exiting->Cr3 = VirtualMemoryKernelCr3();
    Exiting->Waiting = 0;

    if (ParentIsUserWaiter(Exiting->ParentId)) {
        Exiting->State = TASK_ZOMBIE;
        if (!WakeWaitingParent(Exiting)) {
            /* 父用户进程稍后 wait */
        }
    } else {
        Exiting->State = TASK_UNUSED;
        Exiting->Frame = 0;
        Exiting->ParentId = -1;
        gTaskCount--;
    }

    Next = FindRunnable();
    if (!Next) {
        ConsoleWrite("sched: no runnable task after exit\n");
        for (;;) {
            HalCpuPark();
        }
    }
    gCurrent = Next;
    ActivateTask(gCurrent);
    return SchedResumeFrame(gCurrent);
}

UINT64 SchedulerFork(struct INT_FRAME *Frame) {
    VM_ADDR_SPACE *ChildSpace;
    TASK *Parent;
    INT32 ParentSlot;
    int Child;
    UINT8 *Top;
    struct INT_FRAME *CF;
    UINTN j;

    Parent = gCurrent;
    ParentSlot = TaskSlot(Parent);
    if (Parent == 0 || ParentSlot < 0 || !Parent->IsUser || !Parent->UserSpace) {
        Frame->Rax = (UINT64)(INT64)-1;
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
        Frame->Rax = (UINT64)(INT64)-1;
        return 0;
    }

    /* clone 栈较深，先写父返回值；ParentSlot 不依赖可能被栈踩踏的 Parent 指针 */
    Frame->Rax = (UINT64)(UINT32)(Child + 1);

    ChildSpace = VirtualMemorySpaceClone(Parent->UserSpace);
    if (!ChildSpace) {
        Frame->Rax = (UINT64)(INT64)-1;
        return 0;
    }

    Top = gTasks[Child].Stack + sizeof(gTasks[Child].Stack);
    CF = (struct INT_FRAME *)(Top - sizeof(struct INT_FRAME));
    for (j = 0; j < sizeof(struct INT_FRAME); j++) {
        ((UINT8 *)CF)[j] = ((UINT8 *)Frame)[j];
    }
    CF->Rax = 0; /* 子进程 fork 返回 0 */

    gTasks[Child].Frame = CF;
    gTasks[Child].State = TASK_READY;
    gTasks[Child].Ticks = 0;
    gTasks[Child].Cr3 = VirtualMemorySpaceCr3(ChildSpace);
    gTasks[Child].IsUser = 1;
    gTasks[Child].Started = 0;
    gTasks[Child].UserSpace = ChildSpace;
    gTasks[Child].ParentId = ParentSlot;
    gTasks[Child].ExitCode = 0;
    gTasks[Child].Waiting = 0;
    TaskClearFds(&gTasks[Child]);
    CopyName(&gTasks[Child], Parent->Name);
    gTaskCount++;

    Frame->Rax = (UINT64)(UINT32)(Child + 1);
    Parent->Frame = Frame;
    return 0;
}

UINT64 SchedulerWait(struct INT_FRAME *Frame) {
    TASK *Self;
    INT32 MyId;
    int i;
    int Live = 0;
    TASK *Next;

    Self = gCurrent;
    if (Self == 0 || !Self->IsUser) {
        Frame->Rax = (UINT64)(INT64)-1;
        return 0;
    }
    MyId = TaskSlot(Self);

    for (i = 0; i < MAX_TASKS; i++) {
        if (gTasks[i].State == TASK_ZOMBIE && gTasks[i].ParentId == MyId) {
            INT32 Code = gTasks[i].ExitCode;
            INT32 Cid = TaskSlot(&gTasks[i]);
            ReapZombie(&gTasks[i]);
            Frame->Rax = (UINT64)(UINT32)Cid;
            Frame->Rdx = (UINT64)(UINT32)Code;
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
        Frame->Rax = (UINT64)(INT64)-1; /* 无子进程 */
        return 0;
    }

    /* 有存活子进程：阻塞，直到某子进程 exit 经 WakeWaitingParent 写入 Frame */
    Self->Frame = Frame;
    Self->Waiting = 1;
    Self->State = TASK_BLOCKED;

    Next = FindRunnable();
    if (!Next) {
        ConsoleWrite("sched: wait with no runnable task\n");
        for (;;) {
            HalCpuPark();
        }
    }
    gCurrent = Next;
    ActivateTask(gCurrent);
    return SchedResumeFrame(gCurrent);
}

/* 主动让出 CPU */
UINT64 SchedulerYield(struct INT_FRAME *Frame) {
    TASK *Next;

    if (gCurrent == 0) {
        return 0;
    }
    gCurrent->Frame = Frame;
    if (gCurrent->State == TASK_RUNNING) {
        gCurrent->State = TASK_READY;
    }
    Next = PickNext();
    if (Next == gCurrent) {
        gCurrent->State = TASK_RUNNING;
        return 0;
    }
    gCurrent = Next;
    ActivateTask(gCurrent);
    return SchedResumeFrame(gCurrent);
}

void SchedulerStart(void) {
    TASK *First = 0;
    for (int i = 0; i < MAX_TASKS; i++) {
        if (gTasks[i].State == TASK_READY) {
            First = &gTasks[i];
            break;
        }
    }
    if (!First) {
        ConsoleWrite("sched: no tasks\n");
        for (;;) {
            HalCpuPark();
        }
    }
    gCurrent = First;
    gCurrent->State = TASK_RUNNING;
    if (gCurrent->Cr3 != 0) {
        VirtualMemoryLoadCr3(gCurrent->Cr3);
    }
    HalIrqDisable();
    HalTimerStart();
    gCurrent->Started = 1;
    DebugWrite("sched: timer on, entering tasks\n");
    SchedulerEnter(gCurrent->Frame);
}

TASK *SchedulerCurrent(void) {
    return gCurrent;
}

int SchedulerTaskCount(void) {
    return gTaskCount;
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
    return T->Frame->Rip;
}
