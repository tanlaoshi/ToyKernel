/*
 * PhysicalMemoryBitmap.c — first-fit 位图政策（PR-MEM-move）。
 * 锁 / Lookup / DirectBytes / 段表留框架。
 */
#include "MemoryOps.h"
#include "PhysicalMemoryPrivate.h"
#include "Debug.h"

static void BitmapInit(void) { }

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
    Seg = PmmLookup(Phys, &Idx);
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
    Seg = PmmLookup(Phys, &Idx);
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
    Seg = PmmLookup(Phys, &Idx);
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
