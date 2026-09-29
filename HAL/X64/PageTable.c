/*
 * PageTable.c — HAL/X64 页表 Hal* API（PR-S3-pagetable-1）
 */
/*
 * HAL/x86_64/PageTable.c — x86-64 四级页表与 CPU 分页操作
 */
#include "Hal.h"
#include "PhysicalMemory.h"

#include "PageTablePrivate.h"

UINT64 gKernelRoot;



void HalFlushTlb(UINT64 VirtualAddress) {
    __asm__ volatile ("invlpg (%0)" :: "r"(VirtualAddress) : "memory");
}

void HalLoadPageTable(UINT64 Root) {
    __asm__ volatile ("mov %0, %%cr3" :: "r"(Root) : "memory");
}

UINT64 HalGetCurrentPageTable(void) {
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
    UINT64 *Dst = (UINT64 *)(UINTN)(DstRoot & PTE_ADDR_MASK);
    UINT64 *Src = (UINT64 *)(UINTN)(SrcRoot & PTE_ADDR_MASK);
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
    Pml4 = (UINT64 *)(UINTN)(Root & PTE_ADDR_MASK);
    if (!(Pml4[Index] & HAL_PAGE_PRESENT)) {
        return 0;
    }
    OldPdpt = (UINT64 *)(UINTN)(Pml4[Index] & PTE_ADDR_MASK);
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
    UINT64 *Pml4;
    UINT64 *Pdpt;
    UINT64 *OldPd;
    UINT64 *NewPd;
    UINT64 Flags;
    int i;

    /* 私有 PDPT（与内核槽 0 脱钩） */
    if (HalPagePrivatizeRootSlot(Root, 0, Alloc, Ctx) != 0) {
        return -1;
    }
    /*
     * 再私有 PDPT[0]→PD：浅拷贝仍共享内核恒等 2M PD。
     * 若之后拆大页/改 USER，会改到内核页表。用户区虽多在 PDPT[1]，
     * 真机路径上仍见过共享 PD 被改坏 → 二次 exec RSVD。
     */
    Pml4 = (UINT64 *)(UINTN)(Root & PTE_ADDR_MASK);
    if (!(Pml4[0] & HAL_PAGE_PRESENT)) {
        return 0;
    }
    Pdpt = (UINT64 *)(UINTN)(Pml4[0] & PTE_ADDR_MASK);
    if (!(Pdpt[0] & HAL_PAGE_PRESENT)) {
        return 0;
    }
    if (Pdpt[0] & PTE_HUGE) {
        return 0;
    }
    OldPd = (UINT64 *)(UINTN)(Pdpt[0] & PTE_ADDR_MASK);
    NewPd = (UINT64 *)Alloc(Ctx);
    if (!NewPd) {
        return -1;
    }
    for (i = 0; i < 512; i++) {
        NewPd[i] = OldPd[i];
    }
    Flags = Pdpt[0] & 0xFFFULL;
    Pdpt[0] = PagePhys(NewPd) | Flags | HAL_PAGE_PRESENT | HAL_PAGE_WRITABLE;

    /*
     * 用户 VA @0x40000000 落在 PDPT[1]。浅拷贝后若内核曾填过同槽，
     * 会共享内核 PD；User Map/Destroy 会改/释内核页表 → 二次 exec RSVD。
     * 清空用户区间各 PDPT 槽，强制私有 PD/PT 树。
     */
    {
        UINT64 UserStart = HalUserCodeVirt();
        UINT64 UserEnd = HalUserVirtEnd();
        UINT64 Idx0;
        UINT64 Idx1;
        UINT64 Ui;

        if (UserEnd > UserStart) {
            Idx0 = (UserStart >> 30) & 0x1FFull;
            Idx1 = ((UserEnd - 1) >> 30) & 0x1FFull;
            for (Ui = Idx0; Ui <= Idx1 && Ui < 512ull; Ui++) {
                Pdpt[Ui] = 0;
            }
        }
    }
    return 0;
}

/* x86 PTE 软件可用位 bit9：fork COW */
#define HAL_X64_PAGE_COPY_ON_WRITE (1ULL << 9)

int HalPageIsCopyOnWrite(UINT64 Pte) {
    return (Pte & HAL_X64_PAGE_COPY_ON_WRITE) != 0;
}

UINT64 HalPageMarkCopyOnWrite(UINT64 Flags) {
    return (Flags | HAL_X64_PAGE_COPY_ON_WRITE) & ~HAL_PAGE_WRITABLE;
}

int HalPageMap(UINT64 Root, UINT64 VirtualAddress, UINT64 PhysicalAddress, UINT64 Flags,
               HalPageAllocateFunction Alloc, void *Ctx) {
    int User = (Flags & HAL_PAGE_USER) != 0;
    UINT64 *Pml4 = (UINT64 *)(UINTN)(Root & PTE_ADDR_MASK);
    UINT64 *Pte = PageWalk(Pml4, VirtualAddress, 1, User, Alloc, Ctx);
    if (!Pte) {
        return -1;
    }
    *Pte = (PhysicalAddress & PTE_ADDR_MASK) | Flags | HAL_PAGE_PRESENT;
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
    return HalPageGetEntry(HalGetCurrentPageTable(), Virt);
}
