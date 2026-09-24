/*
 * PhysicalMemoryAlloc.c — 段内分配；Bitmap 政策暂留（PR-MEM-ops）。
 */
#include "PhysicalMemoryPrivate.h"
#include "MemoryOps.h"
#include "Debug.h"
#include "SpinLock.h"

static SPIN_LOCK gPhysLock;

/*
 * 返回的页必须落在恒等映射里，才能当指针用。
 * x86 恒等 512MB；arm64/riscv 的 HalPageKernelSetup 恒等 0..4GiB。
 */
#if defined(__x86_64__)
#define PMM_DIRECT_BYTES (512ull << 20)
#else
#define PMM_DIRECT_BYTES (4ull << 30)
#endif

void PmmPhysLockInit(void)
{
    SpinLockInit(&gPhysLock);
}

static UINT32 SegmentFindFreeRange(PMM_SEGMENT *Seg, UINT32 Count)
{
    UINT32 Run = 0;
    UINT32 Start = 0;
    UINT32 i;

    if (Count == 0 || Seg->Bitmap == 0) {
        return (UINT32)-1;
    }
    for (i = 0; i < Seg->PageCount; i++) {
        int Used = (Seg->Bitmap[i / 8] >> (i % 8)) & 1;
        if (!Used) {
            if (Run == 0) {
                Start = i;
            }
            Run++;
            if (Run >= Count) {
                return Start;
            }
        } else {
            Run = 0;
        }
    }
    return (UINT32)-1;
}

static PMM_SEGMENT *Lookup(UINT64 Phys, UINT32 *Idx)
{
    UINT32 SegN = (UINT32)(Phys >> PMM_SEGMENT_SHIFT);
    PMM_SEGMENT *Seg;

    if (SegN >= PMM_SEGMENT_COUNT) {
        return 0;
    }
    Seg = PmmSegment(SegN);
    if (Seg == 0 || Seg->PageCount == 0 || Seg->Bitmap == 0 || Seg->RefCount == 0) {
        return 0;
    }
    *Idx = (UINT32)((Phys - Seg->BasePhys) >> PAGE_SHIFT);
    if (*Idx >= Seg->PageCount) {
        return 0;
    }
    return Seg;
}

static void BitmapInit(void) { }

static void *BitmapAllocPagesLocked(UINT32 Count)
{
    UINT32 s;

    if (Count == 0) {
        return 0;
    }
    for (s = 0; s < PMM_SEGMENT_COUNT; s++) {
        PMM_SEGMENT *Seg = PmmSegment(s);
        UINT32 Idx;
        UINT32 p;
        UINT32 LimitPages;
        UINT64 Room;

        if (Seg->PageCount == 0 || Seg->Bitmap == 0 || Seg->RefCount == 0) {
            continue;
        }
        if (Seg->FreePages < Count || Seg->BasePhys >= PMM_DIRECT_BYTES) {
            continue;
        }
        Room = (PMM_DIRECT_BYTES - Seg->BasePhys) >> PAGE_SHIFT;
        LimitPages = Seg->PageCount;
        if (Room < LimitPages) {
            LimitPages = (UINT32)Room;
        }
        if (LimitPages < Count) {
            continue;
        }
        Idx = SegmentFindFreeRange(Seg, Count);
        if (Idx == (UINT32)-1 || Idx + Count > LimitPages) {
            continue;
        }
        for (p = 0; p < Count; p++) {
            UINT32 i = Idx + p;
            Seg->Bitmap[i / 8] |= (UINT8)(1u << (i % 8));
            Seg->RefCount[i] = 1;
        }
        Seg->FreePages -= Count;
        return (void *)(UINTN)(Seg->BasePhys + ((UINT64)Idx << PAGE_SHIFT));
    }
    return 0;
}

static void BitmapFreePagesLocked(void *Page, UINT32 Count)
{
    UINT64 Phys;
    UINT32 Idx;
    UINT32 p;
    PMM_SEGMENT *Seg;

    if (Page == 0 || Count == 0) {
        return;
    }
    Phys = (UINT64)(UINTN)Page;
    Seg = Lookup(Phys, &Idx);
    if (Seg == 0 || Idx + Count > Seg->PageCount) {
        DebugWrite("pmm: free out of segment\n");
        return;
    }
    for (p = 0; p < Count; p++) {
        UINT32 i = Idx + p;
        int Used = (Seg->Bitmap[i / 8] >> (i % 8)) & 1;

        if (!Used) {
            DebugWrite("pmm: free unused page\n");
            continue;
        }
        if (Seg->RefCount[i] > 1) {
            Seg->RefCount[i]--;
            continue;
        }
        Seg->Bitmap[i / 8] &= (UINT8)~(1u << (i % 8));
        Seg->RefCount[i] = 0;
        Seg->FreePages++;
    }
}

static int BitmapRetainPageLocked(void *Page)
{
    UINT64 Phys;
    UINT32 Idx;
    PMM_SEGMENT *Seg;

    if (Page == 0) {
        return -1;
    }
    Phys = (UINT64)(UINTN)Page;
    Seg = Lookup(Phys, &Idx);
    if (Seg == 0) {
        DebugWrite("pmm: retain no segment\n");
        return -1;
    }
    if (Seg->RefCount[Idx] == 0) {
        DebugWrite("pmm: retain free page\n");
        return -1;
    }
    if (Seg->RefCount[Idx] >= 0xFFFF) {
        return -1;
    }
    Seg->RefCount[Idx]++;
    return 0;
}

static void BitmapReleasePageLocked(void *Page)
{
    UINT64 Phys;
    UINT32 Idx;
    PMM_SEGMENT *Seg;

    if (Page == 0) {
        return;
    }
    Phys = (UINT64)(UINTN)Page;
    Seg = Lookup(Phys, &Idx);
    if (Seg == 0) {
        DebugWrite("pmm: release no segment\n");
        return;
    }
    if (Seg->RefCount[Idx] == 0) {
        DebugWrite("pmm: release free page\n");
        return;
    }
    Seg->RefCount[Idx]--;
    if (Seg->RefCount[Idx] == 0) {
        Seg->Bitmap[Idx / 8] &= (UINT8)~(1u << (Idx % 8));
        Seg->FreePages++;
    }
}

const MEMORY_OPS *MemoryBitmapOps(void)
{
    static const MEMORY_OPS Ops = {
        BitmapInit,
        BitmapAllocPagesLocked,
        BitmapFreePagesLocked,
        BitmapRetainPageLocked,
        BitmapReleasePageLocked,
    };
    return &Ops;
}

void *PhysicalMemoryAllocatePages(UINT32 Count)
{
    void *Ret;

    SpinLockAcquire(&gPhysLock);
    Ret = MemoryOpsGet()->AllocPagesLocked(Count);
    SpinLockRelease(&gPhysLock);
    if (Ret == 0 && Count != 0) {
        DebugWrite("pmm: alloc fail count=");
        DebugHex32(Count);
        DebugWrite("\n");
    }
    return Ret;
}

void *PhysicalMemoryAllocatePage(void) { return PhysicalMemoryAllocatePages(1); }

void PhysicalMemoryFreePages(void *Page, UINT32 Count)
{
    if (Page == 0 || Count == 0 || ((UINT64)(UINTN)Page & (PAGE_SIZE - 1)) != 0) {
        return;
    }
    SpinLockAcquire(&gPhysLock);
    MemoryOpsGet()->FreePagesLocked(Page, Count);
    SpinLockRelease(&gPhysLock);
}

void PhysicalMemoryFreePage(void *Page) { PhysicalMemoryFreePages(Page, 1); }

int PhysicalMemoryRetainPage(void *Page)
{
    int Ok;

    if (Page == 0 || ((UINT64)(UINTN)Page & (PAGE_SIZE - 1)) != 0) {
        return -1;
    }
    SpinLockAcquire(&gPhysLock);
    Ok = MemoryOpsGet()->RetainPageLocked(Page);
    SpinLockRelease(&gPhysLock);
    return Ok;
}

void PhysicalMemoryReleasePage(void *Page)
{
    if (Page == 0 || ((UINT64)(UINTN)Page & (PAGE_SIZE - 1)) != 0) {
        return;
    }
    SpinLockAcquire(&gPhysLock);
    MemoryOpsGet()->ReleasePageLocked(Page);
    SpinLockRelease(&gPhysLock);
}

UINT64 PhysicalMemoryTotalPages(void)
{
    UINT64 Total = 0;
    UINT32 i;
    for (i = 0; i < PMM_SEGMENT_COUNT; i++) {
        Total += PmmSegment(i)->PageCount;
    }
    return Total;
}

UINT64 PhysicalMemoryFreePageCount(void)
{
    UINT64 Total = 0;
    UINT32 i;
    for (i = 0; i < PMM_SEGMENT_COUNT; i++) {
        Total += PmmSegment(i)->FreePages;
    }
    return Total;
}
