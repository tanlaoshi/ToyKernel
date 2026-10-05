/*
 * 人话：开机后起一个教学用的「慢任务」槽，用来演示进度条（25%→100%），
 *       不是商店卸载那种真后台活。任务管理器 / ps 里会看到它的名字。
 *
 * 从哪读：KernelTaskDemoStart → KernelTaskRegister → KernelTaskDemoRun。
 *
 * 别改：槽位最多 4 个；Name 最长 15 字符。Store 装卸不走这张表。
 *
 * 想照着做：暂无短文；对照 KernelMainCommon 里对 KernelTaskDemoStart 的调用。
 */
#include "KernelTask.h"
#include "Scheduler.h"
#include "Hal.h"
#include "Debug.h"

typedef struct {
    char Name[16];
    int State;
    int Progress;
    char Message[40];
    void (*Fn)(void *);
    void *Ctx;
    int TaskId;
} KERNEL_TASK_SLOT;

static KERNEL_TASK_SLOT gSlots[KERNEL_TASK_SLOTS];

static void CopyName(char *Dst, const char *Src) {
    int i = 0;
    if (!Src) {
        Dst[0] = 0;
        return;
    }
    while (Src[i] && i < 15) {
        Dst[i] = Src[i];
        i++;
    }
    Dst[i] = 0;
}

int KernelTaskRegister(const char *Name, void (*Fn)(void *), void *Ctx) {
    int i;
    int Id;

    if (!Name || !Fn) {
        return -1;
    }
    for (i = 0; i < KERNEL_TASK_SLOTS; i++) {
        if (gSlots[i].State == KERNEL_TASK_FREE) {
            break;
        }
    }
    if (i >= KERNEL_TASK_SLOTS) {
        return -1;
    }
    CopyName(gSlots[i].Name, Name);
    gSlots[i].State = KERNEL_TASK_RUN;
    gSlots[i].Progress = 0;
    gSlots[i].Message[0] = 0;
    gSlots[i].Fn = Fn;
    gSlots[i].Ctx = Ctx ? Ctx : &gSlots[i];
    Id = SchedulerCreateKernel(gSlots[i].Name, gSlots[i].Fn, gSlots[i].Ctx);
    gSlots[i].TaskId = Id;
    if (Id < 0) {
        gSlots[i].State = KERNEL_TASK_FREE;
        return -1;
    }
    /* 与 gui 同级，定时器轮转；否则优先级 0 在桌面就绪时跑不到 */
    (void)SchedulerSetPriority((INT32)(Id + 1), SCHED_PRIORITY_SHELL);
    return i;
}

void KernelTaskSetProgress(int Slot, int Progress, const char *Message) {
    int i = 0;
    if (Slot < 0 || Slot >= KERNEL_TASK_SLOTS) {
        return;
    }
    if (Progress < 0) {
        Progress = 0;
    }
    if (Progress > 100) {
        Progress = 100;
    }
    gSlots[Slot].Progress = Progress;
    if (!Message) {
        return;
    }
    while (Message[i] && i < 39) {
        gSlots[Slot].Message[i] = Message[i];
        i++;
    }
    gSlots[Slot].Message[i] = 0;
}

int KernelTaskState(int Slot) {
    if (Slot < 0 || Slot >= KERNEL_TASK_SLOTS) {
        return KERNEL_TASK_FREE;
    }
    return gSlots[Slot].State;
}

int KernelTaskProgress(int Slot) {
    if (Slot < 0 || Slot >= KERNEL_TASK_SLOTS) {
        return 0;
    }
    return gSlots[Slot].Progress;
}

/* 教学：假装一段慢活，每 25% 让出 CPU，让调度器和进度显示能被看见。 */
static void KernelTaskDemoRun(void *Ctx) {
    KERNEL_TASK_SLOT *Slot = (KERNEL_TASK_SLOT *)Ctx;
    int Index = (int)(Slot - gSlots);
    int Percent;

    for (Percent = 25; Percent <= 100; Percent += 25) {
        KernelTaskSetProgress(Index, Percent, "Progress");
        (void)SchedulerCondResched();
    }
    Slot->State = KERNEL_TASK_DONE;
    DebugWrite("KernelTask: Demo Done\n");
}

void KernelTaskDemoStart(void) {
    (void)KernelTaskRegister("KernelTaskDemo", KernelTaskDemoRun, 0);
}
