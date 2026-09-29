/*
 * PageTablePrivate.h — HAL/Arm64 PageTableWalk / PageTable 内部交接（PR-S3-pagetable-1）
 */
#ifndef PAGE_TABLE_PRIVATE_H
#define PAGE_TABLE_PRIVATE_H

#include "Hal.h"

/* AArch64 描述符 */
#define DESC_VALID       (1ULL << 0)
#define DESC_TABLE       (1ULL << 1) /* 页表项或 L3 page */
#define DESC_BLOCK       (0ULL << 1) /* L1/L2 block：bit1=0 */
#define ATTR_NORMAL      (0ULL << 2) /* MAIR Attr0 */
#define ATTR_DEVICE      (1ULL << 2) /* MAIR Attr1 */
#define AP_EL1_RW        (0ULL << 6)
#define AP_EL0_RW        (1ULL << 6)
#define AP_EL1_RO        (2ULL << 6)
#define AP_EL0_RO        (3ULL << 6)
#define SH_INNER         (3ULL << 8)
#define AF_BIT           (1ULL << 10)
#define UXN_BIT          (1ULL << 54)
#define PXN_BIT          (1ULL << 53)
#define SOFT_COW         (1ULL << 56) /* 软件位：fork COW */

#define HAL_PAGE_COPY_ON_WRITE     (1ULL << 9)

extern UINT64 gKernelRoot;
extern UINT64 gCurrentRoot;
extern int gMmuOn;
UINT64 NativeFlagsFromHal(UINT64 HalFlags, int Device);
UINT64 HalViewFromNative(UINT64 Native);
int IsBlock(UINT64 Desc);
UINT64 PagePhys(const void *Ptr);
void PageZero(void *Ptr, UINTN Size);
void *PageAllocTable(void);
UINT64 *PageWalk(UINT64 *L0, UINT64 Virt, int Create, int User,
                 HalPageAllocateFunction Alloc, void *Ctx);
UINT64 *PageLookup(UINT64 Root, UINT64 Virt);

#endif
