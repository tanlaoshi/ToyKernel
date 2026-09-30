/*
 * PhysicalMemoryPrivate.h — Host 单测桩（-I Tests/Stub 优先于 Include/）。
 */
#ifndef PHYSICAL_MEMORY_PRIVATE_H
#define PHYSICAL_MEMORY_PRIVATE_H

#include "PhysicalMemory.h"

#define PMM_SEGMENT_SHIFT     30
#define PMM_SEGMENT_SIZE      (1ULL << PMM_SEGMENT_SHIFT)
#define PMM_SEGMENT_COUNT     4
#define PMM_DIRECT_BYTES      (4ull << 20)

typedef struct {
    UINT64  BasePhys;
    UINT32  PageCount;
    UINT32  FreePages;
    UINT8  *Bitmap;
    UINT16 *RefCount;
} PMM_SEGMENT;

PMM_SEGMENT *PmmSegment(UINT32 Index);
PMM_SEGMENT *PmmLookup(UINT64 Phys, UINT32 *Idx);
void StubMemReset(void);

#endif
