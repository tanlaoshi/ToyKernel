/*
 * 人话：课堂用的「谁先跑、跑在哪一核」调度模板。默认内核不链这个文件，
 *       链的是 round-robin。作业时改这里两处政策即可。
 *
 * 从哪读：SchedulerPriorityOps（表）→ PriorityEnqueue（谁排前面）
 *       → PriorityPickHome（去哪一核）。Init 只清自己的计数。
 *
 * 别改：不要加锁；不要碰 gRunQueue / gIdleSlot / gTasks。
 *       Remove、PickNext 用框架的，不要在这张表里换成自己的。
 *       Enqueue 被叫时这一核的队列锁已经拿着。
 *
 * 想照着做：Documents/开发/如何写一个调度器.md
 *           编进内核 make SCHEDULER=priority
 *           断言 ./Tools/Scripts/runtests.sh scheduler
 */
#include "SchedulerOps.h"
#include "SchedulerPrivate.h"

static volatile int gPriorityHome;

static void PriorityInit(void)
{
    gPriorityHome = 0;
}

/* 高 Priority 靠前；同级保持先来先出。 */
static void PriorityEnqueue(UINT32 Cpu, TASK *T)
{
    TASK **Slot;
    int *Count;
    int Pos;

    if (!T || Cpu >= HAL_MAX_CPUS || IsIdleTask(T) || T->InRunQueue) {
        return;
    }
    SchedulerRunQueueView(Cpu, &Slot, &Count);
    if (*Count >= MAX_TASKS) {
        return;
    }
    Pos = *Count;
    while (Pos > 0) {
        TASK *Prev = Slot[Pos - 1];
        if (!Prev || Prev->Priority >= T->Priority) {
            break;
        }
        Slot[Pos] = Prev;
        Pos--;
    }
    Slot[Pos] = T;
    (*Count)++;
    T->InRunQueue = 1;
    T->HomeCpu = (INT32)Cpu;
}

/* 任务带 Affinity 就钉在那一核，否则按核轮转。 */
static UINT32 PriorityPickHome(const TASK *T)
{
    int Cpus = HalCpuCount();
    int Rr;

    if (Cpus < 1) {
        Cpus = 1;
    }
    if (Cpus > HAL_MAX_CPUS) {
        Cpus = HAL_MAX_CPUS;
    }
    if (T && T->Affinity >= 0 && T->Affinity < Cpus) {
        return (UINT32)T->Affinity;
    }
    Rr = __sync_fetch_and_add(&gPriorityHome, 1);
    if (Rr < 0) {
        Rr = -Rr;
    }
    return (UINT32)(Rr % Cpus);
}

const SCHEDULER_OPS *SchedulerPriorityOps(void)
{
    /* Remove / PickNext 是框架排队与取下一个，不是学生政策。 */
    static const SCHEDULER_OPS Ops = {
        PriorityInit,
        PriorityEnqueue,
        RunQueueRemove,
        PriorityPickHome,
        PickNext,
    };
    return &Ops;
}
