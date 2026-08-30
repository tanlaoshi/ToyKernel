/*
 * hal/x86_64/Page.c — x86-64 四级页表与 CPU 分页操作
 */
#include "hal.h"
#include "Pmm.h"

#define PTE_HUGE (1ULL << 7)

static UINT64 gKernelRoot;

static UINT64 PagePhys(const void *Ptr) {
    return (UINT64)(UINTN)Ptr;
}

static void PageZero(void *Ptr, UINTN Size) {
    UINT8 *B = (UINT8 *)Ptr;
    for (UINTN i = 0; i < Size; i++) {
        B[i] = 0;
    }
}

static void *PageAllocTable(void) {
    void *Page = PmmAllocPage();
    if (!Page) {
        return 0;
    }
    PageZero(Page, PAGE_SIZE);
    return Page;
}

static UINT64 *PageWalk(UINT64 *Pml4, UINT64 Virt, int Create, int User,
                        HalPageAllocFn Alloc, void *Ctx) {
    UINT64 Pml4i = (Virt >> 39) & 0x1FF;
    UINT64 Pdpti = (Virt >> 30) & 0x1FF;
    UINT64 Pdi   = (Virt >> 21) & 0x1FF;
    UINT64 Pti   = (Virt >> 12) & 0x1FF;
    UINT64 TableFlags = HAL_PAGE_PRESENT | HAL_PAGE_WRITABLE;
    if (User) {
        TableFlags |= HAL_PAGE_USER;
    }

    if (!(Pml4[Pml4i] & HAL_PAGE_PRESENT)) {
        if (!Create) {
            return 0;
        }
        UINT64 *NewPdpt = Alloc ? (UINT64 *)Alloc(Ctx) : (UINT64 *)PageAllocTable();
        if (!NewPdpt) {
            return 0;
        }
        if (!Alloc) {
            PageZero(NewPdpt, PAGE_SIZE);
        }
        Pml4[Pml4i] = PagePhys(NewPdpt) | TableFlags;
    } else if (User && !(Pml4[Pml4i] & HAL_PAGE_USER)) {
        Pml4[Pml4i] |= HAL_PAGE_USER;
    }

    UINT64 *Pdpt = (UINT64 *)(UINTN)(Pml4[Pml4i] & ~0xFFFULL);
    if (!(Pdpt[Pdpti] & HAL_PAGE_PRESENT)) {
        if (!Create) {
            return 0;
        }
        UINT64 *NewPd = Alloc ? (UINT64 *)Alloc(Ctx) : (UINT64 *)PageAllocTable();
        if (!NewPd) {
            return 0;
        }
        if (!Alloc) {
            PageZero(NewPd, PAGE_SIZE);
        }
        Pdpt[Pdpti] = PagePhys(NewPd) | TableFlags;
    } else if (User && !(Pdpt[Pdpti] & HAL_PAGE_USER)) {
        Pdpt[Pdpti] |= HAL_PAGE_USER;
    }

    UINT64 *Pd = (UINT64 *)(UINTN)(Pdpt[Pdpti] & ~0xFFFULL);
    if (Pd[Pdi] & PTE_HUGE) {
        return 0;
    }

    UINT64 *Pt = (UINT64 *)(UINTN)(Pd[Pdi] & ~0xFFFULL);
    if (!(Pd[Pdi] & HAL_PAGE_PRESENT)) {
        if (!Create) {
            return 0;
        }
        Pt = Alloc ? (UINT64 *)Alloc(Ctx) : (UINT64 *)PageAllocTable();
        if (!Pt) {
            return 0;
        }
        if (!Alloc) {
            PageZero(Pt, PAGE_SIZE);
        }
        Pd[Pdi] = PagePhys(Pt) | TableFlags;
    } else if (User && !(Pd[Pdi] & HAL_PAGE_USER)) {
        Pd[Pdi] |= HAL_PAGE_USER;
    }

    return &Pt[Pti];
}

static UINT64 *PageLookup(UINT64 Root, UINT64 Virt) {
    UINT64 *Pml4 = (UINT64 *)(UINTN)(Root & ~0xFFFULL);
    return PageWalk(Pml4, Virt, 0, 0, 0, 0);
}

void HalFlushTlb(UINT64 Virt) {
    __asm__ volatile ("invlpg (%0)" :: "r"(Virt) : "memory");
}

void HalLoadPageTable(UINT64 Cr3) {
    __asm__ volatile ("mov %0, %%cr3" :: "r"(Cr3) : "memory");
}

UINT64 HalGetPageTable(void) {
    UINT64 Cr3;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(Cr3));
    return Cr3;
}

void HalPagingEnable(UINT64 RootPhys) {
    UINT64 Cr4;
    __asm__ volatile ("mov %%cr4, %0" : "=r"(Cr4));
    Cr4 |= (1ULL << 5);
    __asm__ volatile ("mov %0, %%cr4" :: "r"(Cr4));

    HalLoadPageTable(RootPhys);

    UINT64 Cr0;
    __asm__ volatile ("mov %%cr0, %0" : "=r"(Cr0));
    Cr0 |= (1ULL << 31);
    __asm__ volatile ("mov %0, %%cr0" :: "r"(Cr0));
}

int HalPageKernelSetup(UINTN IdentityMb) {
    UINT64 *Pml4 = (UINT64 *)PageAllocTable();
    UINT64 *Pdpt = (UINT64 *)PageAllocTable();
    UINT64 *Pd = (UINT64 *)PageAllocTable();
    if (!Pml4 || !Pdpt || !Pd) {
        return -1;
    }

    Pml4[0] = PagePhys(Pdpt) | HAL_PAGE_PRESENT | HAL_PAGE_WRITABLE;
    Pdpt[0] = PagePhys(Pd) | HAL_PAGE_PRESENT | HAL_PAGE_WRITABLE;

    UINTN HugeCount = (IdentityMb * 1024 * 1024) / (2 * 1024 * 1024);
    for (UINTN i = 0; i < HugeCount; i++) {
        Pd[i] = ((UINT64)i << 21) | HAL_PAGE_PRESENT | HAL_PAGE_WRITABLE | PTE_HUGE;
    }

    gKernelRoot = PagePhys(Pml4);
    return 0;
}

UINT64 HalPageKernelRoot(void) {
    return gKernelRoot;
}

UINT64 HalPageRootCreate(HalPageAllocFn Alloc, void *Ctx) {
    if (!Alloc) {
        return 0;
    }
    void *Pml4 = Alloc(Ctx);
    if (!Pml4) {
        return 0;
    }
    return PagePhys(Pml4);
}

void HalPageRootCopy(UINT64 DstRoot, UINT64 SrcRoot) {
    UINT64 *Dst = (UINT64 *)(UINTN)(DstRoot & ~0xFFFULL);
    UINT64 *Src = (UINT64 *)(UINTN)(SrcRoot & ~0xFFFULL);
    for (int i = 0; i < 512; i++) {
        Dst[i] = Src[i];
    }
}

int HalPageMap(UINT64 Root, UINT64 Virt, UINT64 Phys, UINT64 Flags,
               HalPageAllocFn Alloc, void *Ctx) {
    int User = (Flags & HAL_PAGE_USER) != 0;
    UINT64 *Pml4 = (UINT64 *)(UINTN)(Root & ~0xFFFULL);
    UINT64 *Pte = PageWalk(Pml4, Virt, 1, User, Alloc, Ctx);
    if (!Pte) {
        return -1;
    }
    *Pte = (Phys & ~0xFFFULL) | Flags | HAL_PAGE_PRESENT;
    HalFlushTlb(Virt);
    return 0;
}

int HalPageUnmapRange(UINT64 Root, UINT64 Start, UINT64 End) {
    for (UINT64 Virt = Start & ~(UINT64)(PAGE_SIZE - 1); Virt < End; Virt += PAGE_SIZE) {
        UINT64 *Pte = PageLookup(Root, Virt);
        if (!Pte || !(*Pte & HAL_PAGE_PRESENT) || !(*Pte & HAL_PAGE_USER)) {
            continue;
        }
        *Pte = 0;
        HalFlushTlb(Virt);
    }
    return 0;
}

UINT64 HalPageGetEntry(UINT64 Root, UINT64 Virt) {
    UINT64 *Pte = PageLookup(Root, Virt);
    if (!Pte) {
        return 0;
    }
    return *Pte;
}

UINT64 HalPageGetEntryCurrent(UINT64 Virt) {
    return HalPageGetEntry(HalGetPageTable(), Virt);
}
