/*
 * SchedulerRoundRobin.c — 入队排序与选核。锁、队列、steal 留在 SchedulerRunq.c。
 */
#include "SchedulerOps.h"
#include "SchedulerPrivate.h"

static volatile int gRoundRobinHome;

static void RoundRobinInit(void)
{
    gRoundRobinHome = 0;
}

static void RoundRobinEnqueue(UINT32 Cpu, TASK *T)
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

static UINT32 RoundRobinPickHome(const TASK *T)
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
    Rr = __sync_fetch_and_add(&gRoundRobinHome, 1);
    if (Rr < 0) {
        Rr = -Rr;
    }
    return (UINT32)(Rr % Cpus);
}

const SCHEDULER_OPS *SchedulerRoundRobinOps(void)
{
    static const SCHEDULER_OPS Ops = {
        RoundRobinInit,
        RoundRobinEnqueue,
        RunQueueRemove,
        RoundRobinPickHome,
        PickNext,
    };
    return &Ops;
}
