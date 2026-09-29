/*
 * PageTableWalk.c — HAL/RiscV 页表遍历与辅助（PR-S3-pagetable-1）
 */
/*
 * HAL/RiscV/PageTable.c — PR-A10：真 MMU（Sv39 satp）+ 缺页陷阱
 * Common 仍见 HAL_PAGE_*；GetEntry 返回规范化 PTE。
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

UINT64 NativeFromHal(UINT64 Phys, UINT64 HalFlags) {
    UINT64 N = PTE_V | PTE_A | PTE_D | ((Phys >> 12) << PTE_PPN_SHIFT);
    /* 内核可执行代码也在恒等映射内：叶子给 R|W|X */
    N |= PTE_R | PTE_X;
    if (HalFlags & HAL_PAGE_WRITABLE) {
        N |= PTE_W;
    }
    if (HalFlags & HAL_PAGE_USER) {
        N |= PTE_U;
        /* 用户页一般不给 X 除非代码；此处与 x86 一致仅按 Flags */
        if (!(HalFlags & HAL_PAGE_WRITABLE)) {
            /* RO 用户：仍可读 */
        }
    }
    if (HalFlags & HAL_PAGE_COPY_ON_WRITE) {
        N |= PTE_RSW_COW;
        N &= ~PTE_W;
    }
    return N;
}

UINT64 HalViewFromNative(UINT64 Native) {
    UINT64 Out;
    if (!(Native & PTE_V)) {
        return 0;
    }
    /* 非叶子（仅 V）：不当作 present 映射页 */
    if (!(Native & (PTE_R | PTE_W | PTE_X))) {
        return 0;
    }
    Out = ((Native >> PTE_PPN_SHIFT) << 12) | HAL_PAGE_PRESENT;
    if (Native & PTE_W) {
        Out |= HAL_PAGE_WRITABLE;
    }
    if (Native & PTE_U) {
        Out |= HAL_PAGE_USER;
    }
    if (Native & PTE_RSW_COW) {
        Out |= HAL_PAGE_COPY_ON_WRITE;
    }
    return Out;
}

int IsLeaf(UINT64 Pte) {
    return (Pte & PTE_V) && (Pte & (PTE_R | PTE_W | PTE_X));
}

/* Sv39：3 级 VPN[2:0] */
UINT64 *PageWalk(UINT64 *L2, UINT64 Virt, int Create,
                        HalPageAllocateFunction Alloc, void *Ctx) {
    UINT64 i2 = (Virt >> 30) & 0x1FF;
    UINT64 i1 = (Virt >> 21) & 0x1FF;
    UINT64 i0 = (Virt >> 12) & 0x1FF;
    UINT64 *L1;
    UINT64 *L0;

    if (!(L2[i2] & PTE_V)) {
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
        L2[i2] = PTE_V | ((PagePhys(L1) >> 12) << PTE_PPN_SHIFT);
    } else if (IsLeaf(L2[i2])) {
        return 0;
    }
    L1 = (UINT64 *)(UINTN)(((L2[i2] >> PTE_PPN_SHIFT) << 12));

    if (!(L1[i1] & PTE_V)) {
        if (!Create) {
            return 0;
        }
        L0 = Alloc ? (UINT64 *)Alloc(Ctx) : (UINT64 *)PageAllocTable();
        if (!L0) {
            return 0;
        }
        if (!Alloc) {
            PageZero(L0, PAGE_SIZE);
        }
        L1[i1] = PTE_V | ((PagePhys(L0) >> 12) << PTE_PPN_SHIFT);
    } else if (IsLeaf(L1[i1])) {
        return 0;
    }
    L0 = (UINT64 *)(UINTN)(((L1[i1] >> PTE_PPN_SHIFT) << 12));
    return &L0[i0];
}

UINT64 *PageLookup(UINT64 Root, UINT64 Virt) {
    UINT64 *L2 = (UINT64 *)(UINTN)(Root & ~0xFFFULL);
    return PageWalk(L2, Virt, 0, 0, 0);
}

