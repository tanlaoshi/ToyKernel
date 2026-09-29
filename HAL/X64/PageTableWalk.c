/*
 * PageTableWalk.c — HAL/X64 页表遍历与辅助（PR-S3-pagetable-1）
 */
/*
 * HAL/x86_64/PageTable.c — x86-64 四级页表与 CPU 分页操作
 */
#include "Hal.h"
#include "PhysicalMemory.h"

#include "PageTablePrivate.h"

static UINT64 Rdmsr(UINT32 Msr) {
    UINT32 Lo;
    UINT32 Hi;

    __asm__ volatile ("rdmsr" : "=a"(Lo), "=d"(Hi) : "c"(Msr));
    return ((UINT64)Hi << 32) | Lo;
}

static void Wrmsr(UINT32 Msr, UINT64 Value) {
    UINT32 Lo = (UINT32)Value;
    UINT32 Hi = (UINT32)(Value >> 32);

    __asm__ volatile ("wrmsr" :: "c"(Msr), "a"(Lo), "d"(Hi) : "memory");
}

/*
 * PR-G-fb-wc：PA1=WC。索引 PWT=1,PCD=0,PAT=0 → WC；
 * PA0 仍 WB；PA3（PWT|PCD）仍 UC——xHCI PTE_MMIO 不变。
 */
void HalPatApplyWc(void) {
    UINT64 Pat;

    Pat = Rdmsr(MSR_IA32_PAT);
    /* 清 PA1（bits 15:8），写入 WC */
    Pat = (Pat & ~(0xFFULL << 8)) | ((UINT64)PAT_TYPE_WC << 8);
    Wrmsr(MSR_IA32_PAT, Pat);
    __asm__ volatile ("wbinvd" ::: "memory");
}

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

UINT64 *PageWalk(UINT64 *Pml4, UINT64 Virt, int Create, int User,
                        HalPageAllocateFunction Alloc, void *Ctx) {
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

    UINT64 *Pdpt = (UINT64 *)(UINTN)(Pml4[Pml4i] & PTE_ADDR_MASK);
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

    UINT64 *Pd = (UINT64 *)(UINTN)(Pdpt[Pdpti] & PTE_ADDR_MASK);
    /*
     * 2M 大页：Map 需改单页属性（如 LFB→WC）时拆成 4K PT。
     * 保留原 PWT/PCD；2M PAT(bit12) → 4K PAT(bit7)。
     */
    if ((Pd[Pdi] & HAL_PAGE_PRESENT) && (Pd[Pdi] & PTE_HUGE)) {
        UINT64 Huge;
        UINT64 PhysBase;
        UINT64 LeafFlags;
        UINT64 *NewPt;
        UINTN i;

        if (!Create) {
            return 0;
        }
        Huge = Pd[Pdi];
        PhysBase = Huge & 0x000FFFFFFFE00000ULL;
        LeafFlags = HAL_PAGE_PRESENT | HAL_PAGE_WRITABLE;
        if (Huge & HAL_PAGE_USER) {
            LeafFlags |= HAL_PAGE_USER;
        }
        if (Huge & PTE_PWT) {
            LeafFlags |= PTE_PWT;
        }
        if (Huge & PTE_PCD) {
            LeafFlags |= PTE_PCD;
        }
        if (Huge & PTE_PAT2M) {
            LeafFlags |= PTE_HUGE; /* 4K：bit7 = PAT */
        }
        NewPt = Alloc ? (UINT64 *)Alloc(Ctx) : (UINT64 *)PageAllocTable();
        if (!NewPt) {
            return 0;
        }
        if (!Alloc) {
            PageZero(NewPt, PAGE_SIZE);
        }
        for (i = 0; i < 512; i++) {
            NewPt[i] = (PhysBase + ((UINT64)i << 12)) | LeafFlags;
        }
        Pd[Pdi] = PagePhys(NewPt) | TableFlags;
        for (i = 0; i < 512; i++) {
            HalFlushTlb(PhysBase + ((UINT64)i << 12));
        }
    }

    UINT64 *Pt = (UINT64 *)(UINTN)(Pd[Pdi] & PTE_ADDR_MASK);
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

UINT64 *PageLookup(UINT64 Root, UINT64 Virt) {
    UINT64 *Pml4 = (UINT64 *)(UINTN)(Root & PTE_ADDR_MASK);
    return PageWalk(Pml4, Virt, 0, 0, 0, 0);
}

