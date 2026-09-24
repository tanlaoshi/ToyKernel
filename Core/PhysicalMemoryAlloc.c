/*
 * PhysicalMemoryAlloc.c — 框架：锁、Lookup、公开 API 外壳、Total/FreeCount。
 */
#include "PhysicalMemoryPrivate.h"
#include "MemoryOps.h"
#include "Debug.h"
#include "SpinLock.h"

static SPIN_LOCK gPhysLock;

void PmmPhysLockInit(void)
{
    SpinLockInit(&gPhysLock);
}

PMM_SEGMENT *PmmLookup(UINT64 Phys, UINT32 *Idx)
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
