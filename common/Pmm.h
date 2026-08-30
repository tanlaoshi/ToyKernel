/*
 * Pmm.h — 物理内存管理器（Physical Memory Manager）
 *
 * 解析 UEFI 内存映射，用位图管理 4KB 物理页。当前为恒等映射，无分页。
 */
#ifndef PMM_H
#define PMM_H

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
} PMM_BOOT_INFO;

int PmmInit(const PMM_BOOT_INFO *Info);

void *PmmAllocPage(void);
void *PmmAllocPages(UINT32 Count);
void PmmFreePage(void *Page);
void PmmFreePages(void *Page, UINT32 Count);

UINT64 PmmTotalPages(void);
UINT64 PmmFreePageCount(void);

#endif
