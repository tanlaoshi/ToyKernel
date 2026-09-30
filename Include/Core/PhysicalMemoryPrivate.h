/*
 * PhysicalMemoryPrivate.h — 段表与框架辅助，仅内核内部。不进公开 API。
 */
#ifndef PHYSICAL_MEMORY_PRIVATE_H
#define PHYSICAL_MEMORY_PRIVATE_H

#include "PhysicalMemory.h"
#include "BootInfo.h"

#define PMM_SEGMENT_SHIFT     30
#define PMM_SEGMENT_SIZE      (1ULL << PMM_SEGMENT_SHIFT)
#define PMM_SEGMENT_COUNT     256
#define PMM_PAGES_PER_SEGMENT (PMM_SEGMENT_SIZE / PAGE_SIZE)

/*
 * 返回的页必须落在恒等映射里，才能当指针用。
 * x86 恒等 512MB；arm64/riscv 的 HalPageKernelSetup 恒等 0..4GiB。
 */
#if defined(__x86_64__)
#define PMM_DIRECT_BYTES (512ull << 20)
#else
#define PMM_DIRECT_BYTES (4ull << 30)
#endif

typedef struct {
    UINT64  BasePhys;
    UINT32  PageCount;
    UINT32  FreePages;
    UINT8  *Bitmap;
    UINT16 *RefCount;
} PMM_SEGMENT;

void PmmSegmentInit(const BOOT_INFO *Info);
PMM_SEGMENT *PmmSegment(UINT32 Index);
/* 框架：Phys → 段 + 页下标；政策可调用，勿再加锁 */
PMM_SEGMENT *PmmLookup(UINT64 Phys, UINT32 *Idx);

#endif
