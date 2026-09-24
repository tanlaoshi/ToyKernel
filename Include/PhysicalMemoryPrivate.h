/*
 * PhysicalMemoryPrivate.h — 段表，仅内核内部。不进公开 API。
 */
#ifndef PHYSICAL_MEMORY_PRIVATE_H
#define PHYSICAL_MEMORY_PRIVATE_H

#include "PhysicalMemory.h"
#include "BootInfo.h"

#define PMM_SEGMENT_SHIFT     30
#define PMM_SEGMENT_SIZE      (1ULL << PMM_SEGMENT_SHIFT)
#define PMM_SEGMENT_COUNT     256
#define PMM_PAGES_PER_SEGMENT (PMM_SEGMENT_SIZE / PAGE_SIZE)

typedef struct {
    UINT64  BasePhys;
    UINT32  PageCount;
    UINT32  FreePages;
    UINT8  *Bitmap;
    UINT16 *RefCount;
} PMM_SEGMENT;

void PmmSegmentInit(const BOOT_INFO *Info);
PMM_SEGMENT *PmmSegment(UINT32 Index);

#endif
