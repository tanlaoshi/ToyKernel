/*
 * SchedRunq.c — PR-S-sched-split-1：每核 READY 队列 / steal / PickHome / PickNext
 *
 * 从 Scheduler.c 原样搬家；不改语义。
 */
#include "SchedulerPriv.h"
#include "Hal.h"
#include "SpinLock.h"

static SPIN_LOCK gRunqLock[HAL_MAX_CPUS];
static volatile int gRoundRobinHome;
static volatile UINT64 gStealCount;

typedef struct {
    TASK *Slot[MAX_TASKS];
    int   Count;
} CPU_RUN_QUEUE;

static CPU_RUN_QUEUE gRunq[HAL_MAX_CPUS];

/*
 * 锁序（防死锁）：若同持两把 → 先 gSchedulerLock，再 gRunqLock；
 * 多把 gRunqLock → 按 cpu 下标升序。热路径（timer/yield）可只持 runq 锁。
 */

void RunqInit(void) {
    UINT32 c;
    int i;
    gStealCount = 0;
    gRoundRobinHome = 0;
    for (c = 0; c < HAL_MAX_CPUS; c++) {
        SpinLockInit(&gRunqLock[c]);
        gRunq[c].Count = 0;
        for (i = 0; i < MAX_TASKS; i++) {
            gRunq[c].Slot[i] = 0;
        }
    }
}

/* 调用方已持 gRunqLock[Cpu] */
static void RunqEnqueueLocked(UINT32 Cpu, TASK *T) {
    CPU_RUN_QUEUE *Q;
    int Pos;
    if (!T || Cpu >= HAL_MAX_CPUS || IsIdleTask(T) || T->InRunq) {
        return;
    }
    Q = &gRunq[Cpu];
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
    T->InRunq = 1;
    T->HomeCpu = (INT32)Cpu;
}

void RunqEnqueue(UINT32 Cpu, TASK *T) {
    if (!T || Cpu >= HAL_MAX_CPUS) {
        return;
    }
    SpinLockAcquire(&gRunqLock[Cpu]);
    RunqEnqueueLocked(Cpu, T);
    SpinLockRelease(&gRunqLock[Cpu]);
}

static TASK *RunqDequeueLocked(UINT32 Cpu) {
    CPU_RUN_QUEUE *Q;
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

/* 从队尾偷：与本地队头 dequeue 错开，减冲突 */
static TASK *RunqStealOneLocked(UINT32 Victim) {
    CPU_RUN_QUEUE *Q;
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

static int RunqRemoveFromCpuLocked(UINT32 Cpu, TASK *T) {
    CPU_RUN_QUEUE *Q;
    int i, j;
    if (!T || Cpu >= HAL_MAX_CPUS) {
        return 0;
    }
    Q = &gRunq[Cpu];
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
        return 1;
    }
    return 0;
}

void RunqRemove(TASK *T) {
    UINT32 c;
    if (!T || !T->InRunq) {
        return;
    }
    if (T->HomeCpu >= 0 && (UINT32)T->HomeCpu < HAL_MAX_CPUS) {
        c = (UINT32)T->HomeCpu;
        SpinLockAcquire(&gRunqLock[c]);
        if (RunqRemoveFromCpuLocked(c, T)) {
            SpinLockRelease(&gRunqLock[c]);
            return;
        }
        SpinLockRelease(&gRunqLock[c]);
    }
    for (c = 0; c < HAL_MAX_CPUS; c++) {
        SpinLockAcquire(&gRunqLock[c]);
        if (RunqRemoveFromCpuLocked(c, T)) {
            SpinLockRelease(&gRunqLock[c]);
            return;
        }
        SpinLockRelease(&gRunqLock[c]);
    }
    T->InRunq = 0;
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
        SpinLockAcquire(&gRunqLock[Cpu]);
        T = RunqDequeueLocked(Cpu);
        SpinLockRelease(&gRunqLock[Cpu]);
        if (!T) {
            break;
        }
        if (TaskFitsCpu(T, Cpu)) {
            return T;
        }
        Home = PickHomeCpu(T);
        SpinLockAcquire(&gRunqLock[Home]);
        RunqEnqueueLocked(Home, T);
        SpinLockRelease(&gRunqLock[Home]);
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
        SpinLockAcquire(&gRunqLock[Vic]);
        T = RunqStealOneLocked(Vic);
        SpinLockRelease(&gRunqLock[Vic]);
        if (!T) {
            continue;
        }
        if (TaskFitsCpu(T, Cpu)) {
            __sync_fetch_and_add(&gStealCount, 1);
            return T;
        }
        Home = PickHomeCpu(T);
        SpinLockAcquire(&gRunqLock[Home]);
        RunqEnqueueLocked(Home, T);
        SpinLockRelease(&gRunqLock[Home]);
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
