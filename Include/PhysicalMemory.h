/*
 * PhysicalMemory.h — 物理内存管理器（Physical Memory Manager）
 */
#ifndef PHYSICAL_MEMORY_H
#define PHYSICAL_MEMORY_H

#include "BootTypes.h"

#define PAGE_SHIFT 12
#define PAGE_SIZE  4096

int PhysicalMemoryInit(void);

void *PhysicalMemoryAllocatePage(void);
void *PhysicalMemoryAllocatePages(UINT32 Count);
void PhysicalMemoryFreePage(void *Page);
void PhysicalMemoryFreePages(void *Page, UINT32 Count);

UINT64 PhysicalMemoryTotalPages(void);
UINT64 PhysicalMemoryFreePageCount(void);

#endif
