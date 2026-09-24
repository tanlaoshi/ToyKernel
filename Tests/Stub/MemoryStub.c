/*
 * MemoryStub.c — 假段表：段 0 有 63 页（BasePhys=PAGE_SIZE，避开 NULL）。
 */
#include "PhysicalMemoryPrivate.h"

#include <string.h>

#define STUB_PAGES 63

static UINT8 gBitmap[(STUB_PAGES + 7) / 8];
static UINT16 gRef[STUB_PAGES];
static PMM_SEGMENT gSegs[PMM_SEGMENT_COUNT];

void StubMemReset(void)
{
    UINT32 i;

    memset(gBitmap, 0, sizeof(gBitmap));
    memset(gRef, 0, sizeof(gRef));
    memset(gSegs, 0, sizeof(gSegs));
    gSegs[0].BasePhys = PAGE_SIZE;
    gSegs[0].PageCount = STUB_PAGES;
    gSegs[0].FreePages = STUB_PAGES;
    gSegs[0].Bitmap = gBitmap;
    gSegs[0].RefCount = gRef;
    for (i = 1; i < PMM_SEGMENT_COUNT; i++) {
        gSegs[i].PageCount = 0;
    }
}

PMM_SEGMENT *PmmSegment(UINT32 Index)
{
    if (Index >= PMM_SEGMENT_COUNT) {
        return 0;
    }
    return &gSegs[Index];
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
