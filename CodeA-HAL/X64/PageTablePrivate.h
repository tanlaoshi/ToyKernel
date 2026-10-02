/*
 * PageTablePrivate.h — HAL/X64 PageTableWalk / PageTable 内部交接（PR-S3-pagetable-1）
 */
#ifndef PAGE_TABLE_PRIVATE_H
#define PAGE_TABLE_PRIVATE_H

#include "Hal.h"

#define PTE_HUGE (1ULL << 7)
#define PTE_PWT  (1ULL << 3)
#define PTE_PCD  (1ULL << 4)
#define PTE_PAT2M (1ULL << 12) /* 2M PDE：PAT；4K 时 bit7 为 PAT（与 PS 同号不同级） */
#define PTE_ADDR_MASK 0x000FFFFFFFFFF000ULL

#define MSR_IA32_PAT 0x277u
#define PAT_TYPE_UC  0u
#define PAT_TYPE_WC  1u
#define PAT_TYPE_WT  4u
#define PAT_TYPE_WB  6u
#define PAT_TYPE_UC_MINUS 7u

UINT64 PagePhys(const void *Ptr);
void PageZero(void *Ptr, UINTN Size);
void *PageAllocTable(void);
UINT64 *PageWalk(UINT64 *Pml4, UINT64 Virt, int Create, int User,
                 HalPageAllocateFunction Alloc, void *Ctx);
UINT64 *PageLookup(UINT64 Root, UINT64 Virt);

#endif
