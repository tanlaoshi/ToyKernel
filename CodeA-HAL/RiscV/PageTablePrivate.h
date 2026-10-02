/*
 * PageTablePrivate.h — HAL/RiscV PageTableWalk / PageTable 内部交接（PR-S3-pagetable-1）
 */
#ifndef PAGE_TABLE_PRIVATE_H
#define PAGE_TABLE_PRIVATE_H

#include "Hal.h"

#define PTE_V (1ULL << 0)
#define PTE_R (1ULL << 1)
#define PTE_W (1ULL << 2)
#define PTE_X (1ULL << 3)
#define PTE_U (1ULL << 4)
#define PTE_G (1ULL << 5)
#define PTE_A (1ULL << 6)
#define PTE_D (1ULL << 7)
#define PTE_RSW_COW (1ULL << 8) /* RSW 软件位 */

#define PTE_PPN_SHIFT 10
#define SATP_MODE_SV39 (8ULL << 60)

#define HAL_PAGE_COPY_ON_WRITE (1ULL << 9)

extern UINT64 gKernelRoot;
extern UINT64 gCurrentRoot;
extern int gMmuOn;
UINT64 NativeFromHal(UINT64 Phys, UINT64 HalFlags);
UINT64 HalViewFromNative(UINT64 Native);
int IsLeaf(UINT64 Pte);
UINT64 PagePhys(const void *Ptr);
void PageZero(void *Ptr, UINTN Size);
void *PageAllocTable(void);
UINT64 *PageWalk(UINT64 *L2, UINT64 Virt, int Create,
                 HalPageAllocateFunction Alloc, void *Ctx);
UINT64 *PageLookup(UINT64 Root, UINT64 Virt);

#endif
