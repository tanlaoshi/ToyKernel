/*
 * HAL/x86_64/Page.c — x86-64 四级页表与 CPU 分页操作
 */
#include "Hal.h"
#include "PhysicalMemory.h"

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
    void *Page = PhysicalMemoryAllocatePage();
    if (!Page) {
        return 0;
    }
    PageZero(Page, PAGE_SIZE);
    return Page;
}

static UINT64 *PageWalk(UINT64 *Pml4, UINT64 Virt, int Create, int User,
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

void HalFlushTlb(UINT64 VirtualAddress) {
    __asm__ volatile ("invlpg (%0)" :: "r"(VirtualAddress) : "memory");
}

void HalLoadPageTable(UINT64 Root) {
    __asm__ volatile ("mov %0, %%cr3" :: "r"(Root) : "memory");
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

void HalPagingSelfTest(void) {
    /* x86 缺页路径已在 Arch.c #PF → VirtualMemoryHandlePageFault */
}

int HalPageKernelSetup(UINTN IdentityMegabytes) {
    UINT64 *Pml4 = (UINT64 *)PageAllocTable();
    UINT64 *Pdpt = (UINT64 *)PageAllocTable();
    UINT64 *Pd = (UINT64 *)PageAllocTable();
    if (!Pml4 || !Pdpt || !Pd) {
        return -1;
    }

    Pml4[0] = PagePhys(Pdpt) | HAL_PAGE_PRESENT | HAL_PAGE_WRITABLE;
    Pdpt[0] = PagePhys(Pd) | HAL_PAGE_PRESENT | HAL_PAGE_WRITABLE;

    UINTN HugeCount = (IdentityMegabytes * 1024 * 1024) / (2 * 1024 * 1024);
    for (UINTN i = 0; i < HugeCount; i++) {
        Pd[i] = ((UINT64)i << 21) | HAL_PAGE_PRESENT | HAL_PAGE_WRITABLE | PTE_HUGE;
    }

    gKernelRoot = PagePhys(Pml4);
    return 0;
}

UINT64 HalPageKernelRoot(void) {
    return gKernelRoot;
}

UINT64 HalPageRootCreate(HalPageAllocateFunction Alloc, void *Ctx) {
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

/*
 * 将根页表槽 Root[Index] 换成私有下一级表（x86：PML4→私有 PDPT）。
 * 内核恒等映射与用户空间都落在槽 0；浅拷贝后若不私有化，
 * Map/Unmap 用户页会改到共享表，fork/exit 会互相踩页表。
 */
int HalPagePrivatizeRootSlot(UINT64 Root, UINT32 Index, HalPageAllocateFunction Alloc, void *Ctx) {
    UINT64 *Pml4;
    UINT64 *OldPdpt;
    UINT64 *NewPdpt;
    UINT64 Flags;
    int i;

    if (!Alloc || Index >= 512) {
        return -1;
    }
    Pml4 = (UINT64 *)(UINTN)(Root & ~0xFFFULL);
    if (!(Pml4[Index] & HAL_PAGE_PRESENT)) {
        return 0;
    }
    OldPdpt = (UINT64 *)(UINTN)(Pml4[Index] & ~0xFFFULL);
    NewPdpt = (UINT64 *)Alloc(Ctx);
    if (!NewPdpt) {
        return -1;
    }
    for (i = 0; i < 512; i++) {
        NewPdpt[i] = OldPdpt[i];
    }
    Flags = Pml4[Index] & 0xFFFULL;
    Pml4[Index] = PagePhys(NewPdpt) | Flags | HAL_PAGE_PRESENT | HAL_PAGE_WRITABLE;
    return 0;
}

/*
 * PR-A3：用户根私有化。x86 用户 VA 与恒等映射同属 PML4[0]，必须私有 PDPT。
 */
int HalPagePrepareUserRoot(UINT64 Root, HalPageAllocateFunction Alloc, void *Ctx) {
    return HalPagePrivatizeRootSlot(Root, 0, Alloc, Ctx);
}

/* x86 PTE 软件可用位 bit9：fork COW */
#define HAL_X86_PTE_COW (1ULL << 9)

int HalPageIsCow(UINT64 Pte) {
    return (Pte & HAL_X86_PTE_COW) != 0;
}

UINT64 HalPageMarkCow(UINT64 Flags) {
    return (Flags | HAL_X86_PTE_COW) & ~HAL_PAGE_WRITABLE;
}

int HalPageMap(UINT64 Root, UINT64 VirtualAddress, UINT64 PhysicalAddress, UINT64 Flags,
               HalPageAllocateFunction Alloc, void *Ctx) {
    int User = (Flags & HAL_PAGE_USER) != 0;
    UINT64 *Pml4 = (UINT64 *)(UINTN)(Root & ~0xFFFULL);
    UINT64 *Pte = PageWalk(Pml4, VirtualAddress, 1, User, Alloc, Ctx);
    if (!Pte) {
        return -1;
    }
    *Pte = (PhysicalAddress & ~0xFFFULL) | Flags | HAL_PAGE_PRESENT;
    HalFlushTlb(VirtualAddress);
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
