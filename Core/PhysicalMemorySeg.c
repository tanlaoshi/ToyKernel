/*
 * PhysicalMemorySeg.c — 分段位图表（第 1 刀）。
 * Alloc/Free/Retain/Release 仍走 PhysicalMemory.c 的扁平位图。
 */
#include "PhysicalMemory.h"
#include "BootInfo.h"
#include "Debug.h"

#define PMM_SEGMENT_SHIFT     30
#define PMM_SEGMENT_SIZE      (1ULL << PMM_SEGMENT_SHIFT)
#define PMM_SEGMENT_COUNT     256
#define PMM_PAGES_PER_SEGMENT (PMM_SEGMENT_SIZE / PAGE_SIZE)
#define PMM_BITMAP_BYTES      (PMM_PAGES_PER_SEGMENT / 8)

typedef struct {
    UINT64  BasePhys;
    UINT32  PageCount;
    UINT32  FreePages;
    UINT8  *Bitmap;
    UINT16 *RefCount;
} PMM_SEGMENT;

static PMM_SEGMENT gSegments[PMM_SEGMENT_COUNT];
static UINT8       gSegment0Bitmap[PMM_BITMAP_BYTES];
static UINT16      gSegment0RefCount[PMM_PAGES_PER_SEGMENT];
static UINT64      gBootAllocBase;
static UINT64      gBootAllocNext;

static void *BootAllocate(UINTN Size)
{
    UINT64 Aligned;
    UINT64 Next;
    void *Ptr;

    Aligned = (Size + PAGE_SIZE - 1) & ~((UINT64)PAGE_SIZE - 1);
    if (gBootAllocNext >= PMM_SEGMENT_SIZE ||
        Aligned > PMM_SEGMENT_SIZE - gBootAllocNext) {
        DebugWrite("pmm: boot alloc overflow segment 0\n");
        return 0;
    }
    Next = gBootAllocNext;
    gBootAllocNext = Next + Aligned;
    Ptr = (void *)(UINTN)Next;
    return Ptr;
}

static void ZeroSeg(PMM_SEGMENT *Seg)
{
    Seg->BasePhys = 0;
    Seg->PageCount = 0;
    Seg->FreePages = 0;
    Seg->Bitmap = 0;
    Seg->RefCount = 0;
}

static void FillUsed(UINT8 *Bitmap, UINTN Bytes)
{
    UINTN i;

    for (i = 0; i < Bytes; i++) {
        Bitmap[i] = 0xFF;
    }
}

static void NoteSpan(UINT64 Start, UINT64 End)
{
    while (Start < End) {
        UINT32 Seg = (UINT32)(Start >> PMM_SEGMENT_SHIFT);
        UINT64 Base;
        UINT64 SegEnd;
        UINT64 ChunkEnd;
        UINT32 EndIdx;

        if (Seg >= PMM_SEGMENT_COUNT) {
            break;
        }
        Base = (UINT64)Seg << PMM_SEGMENT_SHIFT;
        SegEnd = Base + PMM_SEGMENT_SIZE;
        ChunkEnd = (End < SegEnd) ? End : SegEnd;
        gSegments[Seg].BasePhys = Base;
        EndIdx = (UINT32)((ChunkEnd - Base + PAGE_SIZE - 1) >> PAGE_SHIFT);
        if (EndIdx > PMM_PAGES_PER_SEGMENT) {
            EndIdx = (UINT32)PMM_PAGES_PER_SEGMENT;
        }
        if (EndIdx > gSegments[Seg].PageCount) {
            gSegments[Seg].PageCount = EndIdx;
        }
        Start = ChunkEnd;
    }
}

static void ClearFree(UINT64 Start, UINT64 End)
{
    while (Start < End) {
        UINT32 Seg = (UINT32)(Start >> PMM_SEGMENT_SHIFT);
        UINT64 Base;
        UINT64 SegEnd;
        UINT64 ChunkEnd;
        UINT64 Page;
        UINT64 PageEnd;
        PMM_SEGMENT *S;

        if (Seg >= PMM_SEGMENT_COUNT) {
            break;
        }
        Base = (UINT64)Seg << PMM_SEGMENT_SHIFT;
        SegEnd = Base + PMM_SEGMENT_SIZE;
        ChunkEnd = (End < SegEnd) ? End : SegEnd;
        S = &gSegments[Seg];
        if (S->PageCount == 0 || S->Bitmap == 0) {
            Start = ChunkEnd;
            continue;
        }
        Page = (Start + PAGE_SIZE - 1) & ~((UINT64)PAGE_SIZE - 1);
        if (Page < Base) {
            Page = Base;
        }
        PageEnd = ChunkEnd & ~((UINT64)PAGE_SIZE - 1);
        while (Page < PageEnd) {
            UINT32 Idx = (UINT32)((Page - Base) >> PAGE_SHIFT);
            UINT8 Mask;

            if (Idx >= S->PageCount) {
                break;
            }
            Mask = (UINT8)(1u << (Idx % 8));
            S->Bitmap[Idx / 8] &= (UINT8)~Mask;
            S->FreePages++;
            Page += PAGE_SIZE;
        }
        Start = ChunkEnd;
    }
}

static void MarkBootUsed(void)
{
    UINT64 Phys;

    for (Phys = gBootAllocBase; Phys < gBootAllocNext; Phys += PAGE_SIZE) {
        UINT32 Idx = (UINT32)(Phys >> PAGE_SHIFT);
        UINT8 Mask;

        if (Idx >= PMM_PAGES_PER_SEGMENT) {
            break;
        }
        Mask = (UINT8)(1u << (Idx % 8));
        if ((gSegment0Bitmap[Idx / 8] & Mask) == 0) {
            gSegment0Bitmap[Idx / 8] |= Mask;
            if (gSegments[0].FreePages > 0) {
                gSegments[0].FreePages--;
            }
        }
    }
}

#if TOY_KERNEL_DEBUG
static void PmmDebugDump(void)
{
    UINT32 i;

    DebugWrite("pmm: segments\n");
    for (i = 0; i < PMM_SEGMENT_COUNT; i++) {
        if (gSegments[i].PageCount == 0) {
            continue;
        }
        DebugWrite("  seg ");
        DebugHex32(i);
        DebugWrite(" pages=");
        DebugHex32(gSegments[i].PageCount);
        DebugWrite(" free=");
        DebugHex32(gSegments[i].FreePages);
        DebugWrite("\n");
    }
}
#endif

static int AllocSegmentMaps(UINT32 Index)
{
    PMM_SEGMENT *S = &gSegments[Index];
    UINT8 *Bitmap;
    UINT16 *Refs;
    UINTN n;

    S->BasePhys = (UINT64)Index << PMM_SEGMENT_SHIFT;
    Bitmap = (UINT8 *)BootAllocate(PMM_BITMAP_BYTES);
    Refs = (UINT16 *)BootAllocate(PMM_PAGES_PER_SEGMENT * sizeof(UINT16));
    if (Bitmap == 0 || Refs == 0) {
        DebugWrite("pmm: segment alloc fail\n");
        S->PageCount = 0;
        S->Bitmap = 0;
        S->RefCount = 0;
        return -1;
    }
    FillUsed(Bitmap, PMM_BITMAP_BYTES);
    for (n = 0; n < PMM_PAGES_PER_SEGMENT; n++) {
        Refs[n] = 0;
    }
    S->Bitmap = Bitmap;
    S->RefCount = Refs;
    return 0;
}

void PmmSegmentInit(const BOOT_INFO *Info)
{
    UINT32 i;
    UINTN n;

    if (Info == 0) {
        return;
    }
    for (i = 0; i < PMM_SEGMENT_COUNT; i++) {
        ZeroSeg(&gSegments[i]);
    }
    gBootAllocNext = (Info->KernelEnd + PAGE_SIZE - 1) & ~((UINT64)PAGE_SIZE - 1);
    gBootAllocBase = gBootAllocNext;

    gSegments[0].BasePhys = 0;
    gSegments[0].Bitmap = gSegment0Bitmap;
    gSegments[0].RefCount = gSegment0RefCount;
    FillUsed(gSegment0Bitmap, PMM_BITMAP_BYTES);
    for (n = 0; n < PMM_PAGES_PER_SEGMENT; n++) {
        gSegment0RefCount[n] = 0;
    }

    for (i = 0; i < Info->RegionCount; i++) {
        UINT64 Start = Info->Regions[i].Phys;
        UINT64 Size = Info->Regions[i].Size;

        if (Size == 0) {
            continue;
        }
        NoteSpan(Start, Start + Size);
    }

    for (i = 1; i < PMM_SEGMENT_COUNT; i++) {
        if (gSegments[i].PageCount == 0) {
            continue;
        }
        AllocSegmentMaps(i);
    }

    for (i = 0; i < Info->RegionCount; i++) {
        UINT64 Start;
        UINT64 Size;

        if (Info->Regions[i].Free == 0) {
            continue;
        }
        Start = Info->Regions[i].Phys;
        Size = Info->Regions[i].Size;
        if (Size == 0) {
            continue;
        }
        ClearFree(Start, Start + Size);
    }
    MarkBootUsed();

#if TOY_KERNEL_DEBUG
    PmmDebugDump();
#endif
}
