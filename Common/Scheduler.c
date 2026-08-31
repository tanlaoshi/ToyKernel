/*
 * Scheduler.c — 抢占式任务调度器
 */
#include "Scheduler.h"
#include "hal.h"
#include "Console.h"
#include "Serial.h"
#include "Debug.h"
#include "VirtualMemory.h"

#define SCHED_FIRST_KERNEL  (1ULL << 63)
#define SCHED_FIRST_USER    (1ULL << 62)

static TASK gTasks[MAX_TASKS];
static TASK *gCurrent;
static int gTaskCount;
static int gLastPick;

/* 清空任务表，重置调度状态 */
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
    }
    gCurrent = 0;
    gTaskCount = 0;
    gLastPick = 0;
}

/* 创建任务：在栈顶伪造中断返回帧，Rip 指向 Entry */
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
        int n = 0;
        while (Name[n] && n < 15) {
            gTasks[i].Name[n] = Name[n];
            n++;
        }
        gTasks[i].Name[n] = 0;
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
        int n = 0;
        while (Name[n] && n < 15) {
            gTasks[i].Name[n] = Name[n];
            n++;
        }
        gTasks[i].Name[n] = 0;
        gTaskCount++;
        return i;
    }
    return -1;
}

/* 从当前任务起 round-robin 选取下一个 READY/RUNNING 任务 */
static TASK *PickNext(void) {
    if (gTaskCount <= 1) {
        return gCurrent;
    }
    for (int n = 1; n <= MAX_TASKS; n++) {
        int i = (gLastPick + n) % MAX_TASKS;
        if (gTasks[i].State == TASK_READY || gTasks[i].State == TASK_RUNNING) {
            gLastPick = i;
            return &gTasks[i];
        }
    }
    return gCurrent;
}

/* 返回切换目标：首次进入用高位标记，由 IsrCommon 走 KernelEnter/UserEnter */
static UINT64 SchedResumeFrame(TASK *T) {
    UINT64 Frame = (UINT64)(UINTN)T->Frame;
    if (T->Started) {
        return Frame;
    }
    T->Started = 1;
    if (T->IsUser) {
        return SCHED_FIRST_USER | Frame;
    }
    return SCHED_FIRST_KERNEL | Frame;
}

/* 定时器中断调用：保存当前帧，切换任务，返回新任务 Frame 指针（或 0 表示不切换） */
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
    gCurrent->State = TASK_READY;
    gCurrent = Next;
    gCurrent->State = TASK_RUNNING;
    if (Next->Cr3 != 0) {
        VirtualMemoryLoadCr3(Next->Cr3);
    }
    return SchedResumeFrame(gCurrent);
}

UINT64 SchedulerExitUser(struct INT_FRAME *Frame) {
    (void)Frame;
    if (gCurrent == 0 || !gCurrent->IsUser) {
        for (;;) {
            HalCpuPark();
        }
    }

    ConsoleWrite("syscall: exit — process ");
    ConsoleWrite(gCurrent->Name);
    ConsoleWrite("\n");

    VirtualMemorySpaceDestroy(gCurrent->UserSpace);
    gCurrent->UserSpace = 0;
    gCurrent->State = TASK_UNUSED;
    gTaskCount--;

    TASK *Next = 0;
    for (int n = 1; n <= MAX_TASKS; n++) {
        int i = (gLastPick + n) % MAX_TASKS;
        if (gTasks[i].State == TASK_READY || gTasks[i].State == TASK_RUNNING) {
            gLastPick = i;
            Next = &gTasks[i];
            break;
        }
    }
    if (!Next) {
        ConsoleWrite("sched: no runnable task after exit\n");
        for (;;) {
            HalCpuPark();
        }
    }

    gCurrent = Next;
    gCurrent->State = TASK_RUNNING;
    VirtualMemoryLoadCr3(gCurrent->Cr3);
    return SchedResumeFrame(gCurrent);
}

/* 启动多任务：开定时器，经 SchedulerEnter 跳入第一个任务（不再返回） */
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

/* 返回当前正在运行的任务 */
TASK *SchedulerCurrent(void) {
    return gCurrent;
}

/* 返回已创建任务数量 */
int SchedulerTaskCount(void) {
    return gTaskCount;
}

/* 按槽位索引获取任务（UNUSED 槽返回 NULL） */
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
