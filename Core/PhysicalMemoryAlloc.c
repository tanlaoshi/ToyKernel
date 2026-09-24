/*
 * PhysicalMemoryAlloc.c — 段内分配与引用计数。
 */
#include "PhysicalMemoryPrivate.h"
#include "Debug.h"
#include "SpinLock.h"

static SPIN_LOCK gPhysLock;

/* 与 VirtualMemory.c 的 IDENTITY_MB 一致：返回的页必须能当指针用。 */
#define PMM_DIRECT_BYTES (512ull << 20)

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

void *PhysicalMemoryAllocatePages(UINT32 Count)
{
    UINT32 s;
    void *Ret = 0;

    if (Count == 0) {
        return 0;
    }
    SpinLockAcquire(&gPhysLock);
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
        Ret = (void *)(UINTN)(Seg->BasePhys + ((UINT64)Idx << PAGE_SHIFT));
        break;
    }
    SpinLockRelease(&gPhysLock);
    if (Ret == 0) {
        DebugWrite("pmm: alloc fail count=");
        DebugHex32(Count);
        DebugWrite("\n");
    }
    return Ret;
}

void *PhysicalMemoryAllocatePage(void)
{
    return PhysicalMemoryAllocatePages(1);
}

void PhysicalMemoryFreePages(void *Page, UINT32 Count)
{
    UINT64 Phys;
    UINT32 Idx;
    UINT32 p;
    PMM_SEGMENT *Seg;

    if (Page == 0 || Count == 0) {
        return;
    }
    Phys = (UINT64)(UINTN)Page;
    if ((Phys & (PAGE_SIZE - 1)) != 0) {
        return;
    }
    SpinLockAcquire(&gPhysLock);
    Seg = Lookup(Phys, &Idx);
    if (Seg == 0 || Idx + Count > Seg->PageCount) {
        DebugWrite("pmm: free out of segment\n");
        SpinLockRelease(&gPhysLock);
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
    SpinLockRelease(&gPhysLock);
}

void PhysicalMemoryFreePage(void *Page)
{
    PhysicalMemoryFreePages(Page, 1);
}

int PhysicalMemoryRetainPage(void *Page)
{
    UINT64 Phys;
    UINT32 Idx;
    PMM_SEGMENT *Seg;
    int Ok = -1;

    if (Page == 0) {
        return -1;
    }
    Phys = (UINT64)(UINTN)Page;
    if ((Phys & (PAGE_SIZE - 1)) != 0) {
        return -1;
    }
    SpinLockAcquire(&gPhysLock);
    Seg = Lookup(Phys, &Idx);
    if (Seg == 0) {
        DebugWrite("pmm: retain no segment\n");
    } else if (Seg->RefCount[Idx] == 0) {
        DebugWrite("pmm: retain free page\n");
    } else if (Seg->RefCount[Idx] < 0xFFFF) {
        Seg->RefCount[Idx]++;
        Ok = 0;
    } else {
        Ok = 0;
    }
    SpinLockRelease(&gPhysLock);
    return Ok;
}

void PhysicalMemoryReleasePage(void *Page)
{
    UINT64 Phys;
    UINT32 Idx;
    PMM_SEGMENT *Seg;

    if (Page == 0) {
        return;
    }
    Phys = (UINT64)(UINTN)Page;
    if ((Phys & (PAGE_SIZE - 1)) != 0) {
        return;
    }
    SpinLockAcquire(&gPhysLock);
    Seg = Lookup(Phys, &Idx);
    if (Seg == 0) {
        DebugWrite("pmm: release no segment\n");
        SpinLockRelease(&gPhysLock);
        return;
    }
    if (Seg->RefCount[Idx] == 0) {
        DebugWrite("pmm: release free page\n");
        SpinLockRelease(&gPhysLock);
        return;
    }
    Seg->RefCount[Idx]--;
    if (Seg->RefCount[Idx] == 0) {
        Seg->Bitmap[Idx / 8] &= (UINT8)~(1u << (Idx % 8));
        Seg->FreePages++;
    }
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
