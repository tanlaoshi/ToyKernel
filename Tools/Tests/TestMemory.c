/*
 * TestMemory.c — 对 bitmap 政策断言。宿主 gcc，不链内核。
 */
#include "MemoryOps.h"
#include "PhysicalMemoryPrivate.h"

#include <stdio.h>

static int gFail;

static void Expect(int Cond, const char *Msg)
{
    if (!Cond) {
        printf("fail %s\n", Msg);
        gFail = 1;
    }
}

static void TestAllocFree(const MEMORY_OPS *Ops)
{
    void *A;
    void *B;

    StubMemReset();
    Ops->Init();
    A = Ops->AllocPagesLocked(1);
    Expect(A != 0, "alloc1");
    Expect((UINT64)(UINTN)A == PAGE_SIZE, "first page at PAGE_SIZE");
    Ops->FreePagesLocked(A, 1);
    B = Ops->AllocPagesLocked(1);
    Expect(B == A, "free then alloc same first-fit");
}

static void TestContiguous(const MEMORY_OPS *Ops)
{
    void *A;

    StubMemReset();
    Ops->Init();
    A = Ops->AllocPagesLocked(4);
    Expect(A != 0, "alloc4");
    Expect((UINT64)(UINTN)A == PAGE_SIZE, "contig at base");
    Ops->FreePagesLocked(A, 4);
}

static void TestExhaust(const MEMORY_OPS *Ops)
{
    void *A;

    StubMemReset();
    Ops->Init();
    A = Ops->AllocPagesLocked(1000);
    Expect(A == 0, "too big fails");
    Expect(Ops->AllocPagesLocked(0) == 0, "count0 null");
}

static void TestRetainRelease(const MEMORY_OPS *Ops)
{
    void *A;
    void *B;
    void *C;

    StubMemReset();
    Ops->Init();
    A = Ops->AllocPagesLocked(1);
    Expect(A != 0, "alloc for retain");
    Expect(Ops->RetainPageLocked(A) == 0, "retain ok");
    Ops->FreePagesLocked(A, 1);
    B = Ops->AllocPagesLocked(1);
    Expect(B != 0 && B != A, "retain blocks reclaim");
    Ops->ReleasePageLocked(A);
    C = Ops->AllocPagesLocked(1);
    Expect(C == A, "after release reclaim first-fit");
}

static void TestRetainErrors(const MEMORY_OPS *Ops)
{
    void *A;

    StubMemReset();
    Ops->Init();
    Expect(Ops->RetainPageLocked(0) == -1, "retain null");
    Expect(Ops->RetainPageLocked((void *)(UINTN)(PAGE_SIZE * 2)) == -1, "retain free");
    A = Ops->AllocPagesLocked(1);
    Expect(A != 0, "alloc for sat");
    /* bump to 0xFFFF then one more → -1 */
    {
        UINT32 Idx = 0;
        PMM_SEGMENT *Seg = PmmLookup((UINT64)(UINTN)A, &Idx);
        Expect(Seg != 0, "lookup");
        Seg->RefCount[Idx] = 0xFFFF;
    }
    Expect(Ops->RetainPageLocked(A) == -1, "retain sat");
}

int main(void)
{
#ifdef TOY_MEM_BESTFIT
    const MEMORY_OPS *Ops = MemoryBestFitOps();
#else
    const MEMORY_OPS *Ops = MemoryBitmapOps();
#endif

    MemoryOpsRegister(Ops);
    TestAllocFree(Ops);
    TestContiguous(Ops);
    TestExhaust(Ops);
    TestRetainRelease(Ops);
    TestRetainErrors(Ops);
    if (gFail) {
        return 1;
    }
#ifdef TOY_MEM_BESTFIT
    printf("memory bestfit: ok\n");
#else
    printf("memory: ok\n");
#endif
    return 0;
}
