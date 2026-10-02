/*
 * PageTableWalk.c — HAL/Arm64 页表遍历与辅助（PR-S3-pagetable-1）
 */
/*
 * HAL/Arm64/PageTable.c — PR-A10：真 MMU（TTBR0 / TCR / SCTLR）+ 4K 四级页表
 * Common 仍见 HAL_PAGE_*；HalPageGetEntry 返回规范化 PTE（phys|HAL flags|COW）。
 */
#include "Hal.h"
#include "PhysicalMemory.h"

#include "PageTablePrivate.h"

UINT64 PagePhys(const void *Ptr) {
    return (UINT64)(UINTN)Ptr;
}

void PageZero(void *Ptr, UINTN Size) {
    UINT8 *B = (UINT8 *)Ptr;
    for (UINTN i = 0; i < Size; i++) {
        B[i] = 0;
    }
}

void *PageAllocTable(void) {
    void *Page = PhysicalMemoryAllocatePage();
    if (!Page) {
        return 0;
    }
    PageZero(Page, PAGE_SIZE);
    return Page;
}

UINT64 NativeFlagsFromHal(UINT64 HalFlags, int Device) {
    UINT64 N = DESC_VALID | DESC_TABLE | AF_BIT | SH_INNER;
    if (Device) {
        N |= ATTR_DEVICE;
    } else {
        N |= ATTR_NORMAL;
    }
    if (HalFlags & HAL_PAGE_USER) {
        if (HalFlags & HAL_PAGE_WRITABLE) {
            N |= AP_EL0_RW;
        } else {
            N |= AP_EL0_RO;
        }
    } else {
        if (HalFlags & HAL_PAGE_WRITABLE) {
            N |= AP_EL1_RW;
        } else {
            N |= AP_EL1_RO;
        }
        N |= UXN_BIT;
    }
    if (HalFlags & HAL_PAGE_COPY_ON_WRITE) {
        N |= SOFT_COW;
    }
    return N;
}

UINT64 HalViewFromNative(UINT64 Native) {
    UINT64 Out;
    UINT64 Ap;

    if (!(Native & DESC_VALID)) {
        return 0;
    }
    Out = (Native & 0x0000FFFFFFFFF000ULL) | HAL_PAGE_PRESENT;
    Ap = (Native >> 6) & 3ULL;
    if (Ap == 0 || Ap == 1) {
        Out |= HAL_PAGE_WRITABLE;
    }
    if (Ap == 1 || Ap == 3) {
        Out |= HAL_PAGE_USER;
    }
    if (Native & SOFT_COW) {
        Out |= HAL_PAGE_COPY_ON_WRITE;
    }
    return Out;
}

int IsBlock(UINT64 Desc) {
    return (Desc & DESC_VALID) && !(Desc & DESC_TABLE);
}

UINT64 *PageWalk(UINT64 *L0, UINT64 Virt, int Create, int User,
                        HalPageAllocateFunction Alloc, void *Ctx) {
    UINT64 i0 = (Virt >> 39) & 0x1FF;
    UINT64 i1 = (Virt >> 30) & 0x1FF;
    UINT64 i2 = (Virt >> 21) & 0x1FF;
    UINT64 i3 = (Virt >> 12) & 0x1FF;
    UINT64 TableFlags = DESC_VALID | DESC_TABLE | AF_BIT | ATTR_NORMAL | SH_INNER | AP_EL1_RW;
    UINT64 *L1;
    UINT64 *L2;
    UINT64 *L3;

    (void)User;

    if (!(L0[i0] & DESC_VALID)) {
        if (!Create) {
            return 0;
        }
        L1 = Alloc ? (UINT64 *)Alloc(Ctx) : (UINT64 *)PageAllocTable();
        if (!L1) {
            return 0;
        }
        if (!Alloc) {
            PageZero(L1, PAGE_SIZE);
        }
        L0[i0] = PagePhys(L1) | TableFlags;
    }
    L1 = (UINT64 *)(UINTN)(L0[i0] & 0x0000FFFFFFFFF000ULL);
    if (IsBlock(L1[i1])) {
        return 0;
    }

    if (!(L1[i1] & DESC_VALID)) {
        if (!Create) {
            return 0;
        }
        L2 = Alloc ? (UINT64 *)Alloc(Ctx) : (UINT64 *)PageAllocTable();
        if (!L2) {
            return 0;
        }
        if (!Alloc) {
            PageZero(L2, PAGE_SIZE);
        }
        L1[i1] = PagePhys(L2) | TableFlags;
    }
    L2 = (UINT64 *)(UINTN)(L1[i1] & 0x0000FFFFFFFFF000ULL);
    if (IsBlock(L2[i2])) {
        return 0;
    }

    if (!(L2[i2] & DESC_VALID)) {
        if (!Create) {
            return 0;
        }
        L3 = Alloc ? (UINT64 *)Alloc(Ctx) : (UINT64 *)PageAllocTable();
        if (!L3) {
            return 0;
        }
        if (!Alloc) {
            PageZero(L3, PAGE_SIZE);
        }
        L2[i2] = PagePhys(L3) | TableFlags;
    }
    L3 = (UINT64 *)(UINTN)(L2[i2] & 0x0000FFFFFFFFF000ULL);
    return &L3[i3];
}

UINT64 *PageLookup(UINT64 Root, UINT64 Virt) {
    UINT64 *L0 = (UINT64 *)(UINTN)(Root & ~0xFFFULL);
    return PageWalk(L0, Virt, 0, 0, 0, 0);
}

