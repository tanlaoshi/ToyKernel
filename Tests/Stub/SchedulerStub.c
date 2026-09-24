/*
 * SchedulerStub.c — 队列、空闲任务、CPU 数。不含锁和 steal。
 */
#include "SchedulerPrivate.h"

static TASK *gSlot[HAL_MAX_CPUS][MAX_TASKS];
static int gCount[HAL_MAX_CPUS];
static int gCpus = 1;
static const TASK *gIdle;

int HalCpuCount(void)
{
    return gCpus;
}

void StubSetCpuCount(int Cpus)
{
    if (Cpus < 1) {
        Cpus = 1;
    }
    if (Cpus > HAL_MAX_CPUS) {
        Cpus = HAL_MAX_CPUS;
    }
    gCpus = Cpus;
}

int IsIdleTask(const TASK *T)
{
    return T && T == gIdle;
}

void SchedulerRunQueueView(UINT32 Cpu, TASK ***Slots, int **Count)
{
    *Slots = gSlot[Cpu];
    *Count = &gCount[Cpu];
}

void RunQueueRemove(TASK *T)
{
    UINT32 c;
    int i;
    int j;

    if (!T) {
        return;
    }
    for (c = 0; c < HAL_MAX_CPUS; c++) {
        for (i = 0; i < gCount[c]; i++) {
            if (gSlot[c][i] != T) {
                continue;
            }
            for (j = i + 1; j < gCount[c]; j++) {
                gSlot[c][j - 1] = gSlot[c][j];
            }
            gCount[c]--;
            gSlot[c][gCount[c]] = 0;
            T->InRunQueue = 0;
            return;
        }
    }
    T->InRunQueue = 0;
}

TASK *PickNext(UINT32 Cpu)
{
    TASK *T;
    int i;

    if (Cpu >= HAL_MAX_CPUS || gCount[Cpu] <= 0) {
        return 0;
    }
    T = gSlot[Cpu][0];
    for (i = 1; i < gCount[Cpu]; i++) {
        gSlot[Cpu][i - 1] = gSlot[Cpu][i];
    }
    gCount[Cpu]--;
    gSlot[Cpu][gCount[Cpu]] = 0;
    if (T) {
        T->InRunQueue = 0;
    }
    return T;
}
