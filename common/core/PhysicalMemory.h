/*
 * PhysicalMemory.h — 物理内存管理器（Physical Memory Manager）
 */
#ifndef PHYSICAL_MEMORY_H
#define PHYSICAL_MEMORY_H

#include "BootConfig.h"

#define PAGE_SHIFT 12
#define PAGE_SIZE  4096

/* PMM 初始化时传入的保留区信息 */
typedef struct {
    MEMORY_MAP *Map;
    UINT64      KernelStart;
    UINT64      KernelEnd;
    UINT64      BootConfigPhys;
    UINT64      FrameBufferBase;
    UINT64      FrameBufferSize;
} PHYSICAL_MEMORY_BOOT_INFO;

int PhysicalMemoryInit(const PHYSICAL_MEMORY_BOOT_INFO *Info);

void *PhysicalMemoryAllocatePage(void);
void *PhysicalMemoryAllocatePages(UINT32 Count);
void PhysicalMemoryFreePage(void *Page);
void PhysicalMemoryFreePages(void *Page, UINT32 Count);

UINT64 PhysicalMemoryTotalPages(void);
UINT64 PhysicalMemoryFreePageCount(void);

#endif
