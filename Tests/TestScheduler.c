/*
 * TestScheduler.c — 对 round-robin 政策断言。宿主 gcc 编译，不链内核。
 */
#include "SchedulerOps.h"
#include "SchedulerPrivate.h"

#include <stdio.h>

static int gFail;

static void Expect(int Cond, const char *Msg)
{
    if (!Cond) {
        printf("fail %s\n", Msg);
        gFail = 1;
    }
}

static TASK *NewTask(INT32 Priority, INT32 Affinity)
{
    static TASK Pool[MAX_TASKS];
    static int Used;
    TASK *T;

    T = &Pool[Used++];
    T->Priority = Priority;
    T->Affinity = Affinity;
    T->HomeCpu = -1;
    T->InRunQueue = 0;
    return T;
}

static void TestPriority(const SCHEDULER_OPS *Ops)
{
    TASK *Low = NewTask(0, -1);
    TASK *High = NewTask(8, -1);

    Ops->Init();
    Ops->Enqueue(0, Low);
    Ops->Enqueue(0, High);
    Expect(PickNext(0) == High, "high first");
    Expect(PickNext(0) == Low, "then low");
    Expect(PickNext(0) == 0, "empty");
}

static void TestFifo(const SCHEDULER_OPS *Ops)
{
    TASK *First = NewTask(1, -1);
    TASK *Second = NewTask(1, -1);

    Ops->Init();
    Ops->Enqueue(0, First);
    Ops->Enqueue(0, Second);
    Expect(PickNext(0) == First, "fifo first");
    Expect(PickNext(0) == Second, "fifo second");
}

static void TestHome(const SCHEDULER_OPS *Ops)
{
    TASK *Pinned = NewTask(0, 1);
    TASK *Free = NewTask(0, -1);
    UINT32 A;
    UINT32 B;

    Ops->Init();
    StubSetCpuCount(2);
    Expect(Ops->PickHome(Pinned) == 1, "affinity");
    A = Ops->PickHome(Free);
    B = Ops->PickHome(Free);
    Expect(A == 0 && B == 1, "round robin home");
}

static void TestRemove(const SCHEDULER_OPS *Ops)
{
    TASK *T = NewTask(3, -1);

    Ops->Init();
    Ops->Enqueue(0, T);
    Ops->Remove(T);
    Expect(T->InRunQueue == 0, "removed flag");
    Expect(PickNext(0) == 0, "removed from queue");
}

int main(void)
{
#ifdef TOY_SCHED_PRIORITY
    const SCHEDULER_OPS *Ops = SchedulerPriorityOps();
#else
    const SCHEDULER_OPS *Ops = SchedulerRoundRobinOps();
#endif

    SchedulerOpsRegister(Ops);
    TestPriority(Ops);
    TestFifo(Ops);
    TestHome(Ops);
    TestRemove(Ops);
    if (gFail) {
        return 1;
    }
    printf("scheduler: ok\n");
    return 0;
}
