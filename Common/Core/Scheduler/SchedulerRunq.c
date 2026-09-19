/*
 * SchedulerRunq.c — PR-S-sched-split-1：每核 READY 队列 / steal / PickHome / PickNext
 *
 * 从 Scheduler.c 原样搬家；不改语义。
 */
#include "SchedulerPrivate.h"
#include "Hal.h"
#include "SpinLock.h"

static SPIN_LOCK gRunQueueLock[HAL_MAX_CPUS];
static volatile int gRoundRobinHome;
static volatile UINT64 gStealCount;

typedef struct {
    TASK *Slot[MAX_TASKS];
    int   Count;
} CPU_RUN_QUEUE;

static CPU_RUN_QUEUE gRunQueue[HAL_MAX_CPUS];

/*
 * 锁序（防死锁）：若同持两把 → 先 gSchedulerLock，再 gRunQueueLock；
 * 多把 gRunQueueLock → 按 cpu 下标升序。热路径（timer/yield）可只持 runq 锁。
 */

void RunQueueInitialize(void) {
    UINT32 c;
    int i;
    gStealCount = 0;
    gRoundRobinHome = 0;
    for (c = 0; c < HAL_MAX_CPUS; c++) {
        SpinLockInit(&gRunQueueLock[c]);
        gRunQueue[c].Count = 0;
        for (i = 0; i < MAX_TASKS; i++) {
            gRunQueue[c].Slot[i] = 0;
        }
    }
}

/* 调用方已持 gRunQueueLock[Cpu] */
static void RunQueueEnqueueLocked(UINT32 Cpu, TASK *T) {
    CPU_RUN_QUEUE *Q;
    int Pos;
    if (!T || Cpu >= HAL_MAX_CPUS || IsIdleTask(T) || T->InRunQueue) {
        return;
    }
    Q = &gRunQueue[Cpu];
    if (Q->Count >= MAX_TASKS) {
        return;
    }
    /* 高 Priority 靠前；同级 FIFO——队尾偷任务偏向低优先级 */
    Pos = Q->Count;
    while (Pos > 0 && Q->Slot[Pos - 1]->Priority < T->Priority) {
        Q->Slot[Pos] = Q->Slot[Pos - 1];
        Pos--;
    }
    Q->Slot[Pos] = T;
    Q->Count++;
    T->InRunQueue = 1;
    T->HomeCpu = (INT32)Cpu;
}

void RunQueueEnqueue(UINT32 Cpu, TASK *T) {
    if (!T || Cpu >= HAL_MAX_CPUS) {
        return;
    }
    SpinLockAcquire(&gRunQueueLock[Cpu]);
    RunQueueEnqueueLocked(Cpu, T);
    SpinLockRelease(&gRunQueueLock[Cpu]);
}

static TASK *RunQueueDequeueLocked(UINT32 Cpu) {
    CPU_RUN_QUEUE *Q;
    TASK *T;
    int i;
    if (Cpu >= HAL_MAX_CPUS) {
        return 0;
    }
    Q = &gRunQueue[Cpu];
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
        T->InRunQueue = 0;
    }
    return T;
}

/* 从队尾偷：与本地队头 dequeue 错开，减冲突 */
static TASK *RunQueueStealOneLocked(UINT32 Victim) {
    CPU_RUN_QUEUE *Q;
    TASK *T;
    if (Victim >= HAL_MAX_CPUS) {
        return 0;
    }
    Q = &gRunQueue[Victim];
    if (Q->Count <= 0) {
        return 0;
    }
    T = Q->Slot[Q->Count - 1];
    Q->Count--;
    Q->Slot[Q->Count] = 0;
    if (T) {
        T->InRunQueue = 0;
    }
    return T;
}

static int RunQueueRemoveFromCpuLocked(UINT32 Cpu, TASK *T) {
    CPU_RUN_QUEUE *Q;
    int i, j;
    if (!T || Cpu >= HAL_MAX_CPUS) {
        return 0;
    }
    Q = &gRunQueue[Cpu];
    for (i = 0; i < Q->Count; i++) {
        if (Q->Slot[i] != T) {
            continue;
        }
        for (j = i + 1; j < Q->Count; j++) {
            Q->Slot[j - 1] = Q->Slot[j];
        }
        Q->Count--;
        Q->Slot[Q->Count] = 0;
        T->InRunQueue = 0;
        return 1;
    }
    return 0;
}

void RunQueueRemove(TASK *T) {
    UINT32 c;
    if (!T || !T->InRunQueue) {
        return;
    }
    if (T->HomeCpu >= 0 && (UINT32)T->HomeCpu < HAL_MAX_CPUS) {
        c = (UINT32)T->HomeCpu;
        SpinLockAcquire(&gRunQueueLock[c]);
        if (RunQueueRemoveFromCpuLocked(c, T)) {
            SpinLockRelease(&gRunQueueLock[c]);
            return;
        }
        SpinLockRelease(&gRunQueueLock[c]);
    }
    for (c = 0; c < HAL_MAX_CPUS; c++) {
        SpinLockAcquire(&gRunQueueLock[c]);
        if (RunQueueRemoveFromCpuLocked(c, T)) {
            SpinLockRelease(&gRunQueueLock[c]);
            return;
        }
        SpinLockRelease(&gRunQueueLock[c]);
    }
    T->InRunQueue = 0;
}

UINT32 PickHomeCpu(const TASK *T) {
    int Cpus = HalCpuCount();
    UINT32 Home;
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
    Home = (UINT32)(Rr % Cpus);
    return Home;
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

TASK *PickNext(UINT32 Cpu) {
    TASK *Idle = (Cpu < HAL_MAX_CPUS) ? gIdleTask[Cpu] : 0;
    TASK *T;
    int Cpus;
    int v;
    UINT32 Home;

    /* 1) 本核队列（只持本核 runq 锁） */
    for (;;) {
        SpinLockAcquire(&gRunQueueLock[Cpu]);
        T = RunQueueDequeueLocked(Cpu);
        SpinLockRelease(&gRunQueueLock[Cpu]);
        if (!T) {
            break;
        }
        if (TaskFitsCpu(T, Cpu)) {
            return T;
        }
        Home = PickHomeCpu(T);
        SpinLockAcquire(&gRunQueueLock[Home]);
        RunQueueEnqueueLocked(Home, T);
        SpinLockRelease(&gRunQueueLock[Home]);
    }

    /* 2) 从其它核偷（一次只持一把 victim 锁） */
    Cpus = HalCpuCount();
    if (Cpus < 1) {
        Cpus = 1;
    }
    if (Cpus > HAL_MAX_CPUS) {
        Cpus = HAL_MAX_CPUS;
    }
    for (v = 1; v < Cpus; v++) {
        UINT32 Vic = (Cpu + (UINT32)v) % (UINT32)Cpus;
        SpinLockAcquire(&gRunQueueLock[Vic]);
        T = RunQueueStealOneLocked(Vic);
        SpinLockRelease(&gRunQueueLock[Vic]);
        if (!T) {
            continue;
        }
        if (TaskFitsCpu(T, Cpu)) {
            __sync_fetch_and_add(&gStealCount, 1);
            return T;
        }
        Home = PickHomeCpu(T);
        SpinLockAcquire(&gRunQueueLock[Home]);
        RunQueueEnqueueLocked(Home, T);
        SpinLockRelease(&gRunQueueLock[Home]);
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

UINT64 SchedulerStealCount(void) {
    return gStealCount;
}
