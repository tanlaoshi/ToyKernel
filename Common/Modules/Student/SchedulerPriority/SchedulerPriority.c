/*
 * SchedulerPriority.c — 学生调度模板。
 *
 * 改 Enqueue 决定谁排在前面，改 PickHome 决定新任务去哪一核。
 * 不要在这里加锁，不要碰 gRunQueue / gIdleSlot。队列由框架在调用前锁好。
 * 编进内核：make SCHEDULER=priority
 * 只跑断言：./Scripts/runtests.sh scheduler
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
    static const SCHEDULER_OPS Ops = {
        PriorityInit,
        PriorityEnqueue,
        RunQueueRemove,
        PriorityPickHome,
        PickNext,
    };
    return &Ops;
}
