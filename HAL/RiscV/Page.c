/*
 * HAL/RiscV/Page.c — PR-A10：真 MMU（Sv39 satp）+ 缺页陷阱
 * Common 仍见 HAL_PAGE_*；GetEntry 返回规范化 PTE。
 */
#include "Hal.h"
#include "PhysicalMemory.h"

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

#define HAL_VIEW_COW (1ULL << 9)

static UINT64 gKernelRoot;
static UINT64 gCurrentRoot;
static int gMmuOn;

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

static UINT64 NativeFromHal(UINT64 Phys, UINT64 HalFlags) {
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
    if (HalFlags & HAL_VIEW_COW) {
        N |= PTE_RSW_COW;
        N &= ~PTE_W;
    }
    return N;
}

static UINT64 HalViewFromNative(UINT64 Native) {
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
        Out |= HAL_VIEW_COW;
    }
    return Out;
}

static int IsLeaf(UINT64 Pte) {
    return (Pte & PTE_V) && (Pte & (PTE_R | PTE_W | PTE_X));
}

/* Sv39：3 级 VPN[2:0] */
static UINT64 *PageWalk(UINT64 *L2, UINT64 Virt, int Create,
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

static UINT64 *PageLookup(UINT64 Root, UINT64 Virt) {
    UINT64 *L2 = (UINT64 *)(UINTN)(Root & ~0xFFFULL);
    return PageWalk(L2, Virt, 0, 0, 0);
}

void HalFlushTlb(UINT64 VirtualAddress) {
    if (!gMmuOn) {
        return;
    }
    __asm__ volatile("sfence.vma %0, zero" :: "r"(VirtualAddress) : "memory");
}

void HalLoadPageTable(UINT64 Root) {
    gCurrentRoot = Root;
    if (!gMmuOn) {
        return;
    }
    __asm__ volatile(
        "csrw satp, %0\n"
        "sfence.vma\n"
        :: "r"(SATP_MODE_SV39 | (Root >> 12)) : "memory");
}

UINT64 HalGetPageTable(void) {
    if (gMmuOn) {
        UINT64 Satp;
        __asm__ volatile("csrr %0, satp" : "=r"(Satp));
        return (Satp & ((1ULL << 44) - 1)) << 12;
    }
    return gCurrentRoot ? gCurrentRoot : gKernelRoot;
}

void HalTrapVectorInstall(void);

void HalPagingEnable(UINT64 RootPhys) {
    HalTrapVectorInstall();
    gCurrentRoot = RootPhys;
    gKernelRoot = RootPhys;
    __asm__ volatile(
        "csrw satp, %0\n"
        "sfence.vma\n"
        :: "r"(SATP_MODE_SV39 | (RootPhys >> 12)) : "memory");
    gMmuOn = 1;
    HalSerialWrite("vmm: RiscV Sv39 on\n");
    HalPagingSelfTest();
}

/*
 * Sv39 恒等：0..4GiB，2MiB megapages（L1 leaf）。
 * 低 1GiB 与其余同为 R|W|X（virt MMIO 可；真机可再拆）。
 */
int HalPageKernelSetup(UINTN IdentityMegabytes) {
    UINT64 *L2;
    UINTN GiB = 4;
    UINTN g;
    UINTN b;

    (void)IdentityMegabytes;
    L2 = (UINT64 *)PageAllocTable();
    if (!L2) {
        return -1;
    }

    for (g = 0; g < GiB; g++) {
        UINT64 *L1 = (UINT64 *)PageAllocTable();
        if (!L1) {
            return -1;
        }
        L2[g] = PTE_V | ((PagePhys(L1) >> 12) << PTE_PPN_SHIFT);
        for (b = 0; b < 512; b++) {
            UINT64 Pa = ((UINT64)g << 30) + ((UINT64)b << 21);
            /* 2MiB leaf @ L1：V|R|W|X|A|D + PPN of 2MiB page */
            L1[b] = PTE_V | PTE_R | PTE_W | PTE_X | PTE_A | PTE_D |
                    ((Pa >> 12) << PTE_PPN_SHIFT);
        }
    }

    gKernelRoot = PagePhys(L2);
    gCurrentRoot = gKernelRoot;
    return 0;
}

UINT64 HalPageKernelRoot(void) {
    return gKernelRoot;
}

UINT64 HalPageRootCreate(HalPageAllocateFunction Alloc, void *Ctx) {
    if (!Alloc) {
        return 0;
    }
    void *L2 = Alloc(Ctx);
    if (!L2) {
        return 0;
    }
    return PagePhys(L2);
}

void HalPageRootCopy(UINT64 DstRoot, UINT64 SrcRoot) {
    UINT64 *Dst = (UINT64 *)(UINTN)(DstRoot & ~0xFFFULL);
    UINT64 *Src = (UINT64 *)(UINTN)(SrcRoot & ~0xFFFULL);
    for (int i = 0; i < 512; i++) {
        Dst[i] = Src[i];
    }
}

int HalPagePrivatizeRootSlot(UINT64 Root, UINT32 Index, HalPageAllocateFunction Alloc, void *Ctx) {
    UINT64 *L2;
    UINT64 *Old;
    UINT64 *New;
    int i;

    if (!Alloc || Index >= 512) {
        return -1;
    }
    L2 = (UINT64 *)(UINTN)(Root & ~0xFFFULL);
    if (!(L2[Index] & PTE_V)) {
        return 0;
    }
    if (IsLeaf(L2[Index])) {
        return -1;
    }
    Old = (UINT64 *)(UINTN)(((L2[Index] >> PTE_PPN_SHIFT) << 12));
    New = (UINT64 *)Alloc(Ctx);
    if (!New) {
        return -1;
    }
    for (i = 0; i < 512; i++) {
        New[i] = Old[i];
    }
    L2[Index] = PTE_V | ((PagePhys(New) >> 12) << PTE_PPN_SHIFT);
    return 0;
}

int HalPagePrepareUserRoot(UINT64 Root, HalPageAllocateFunction Alloc, void *Ctx) {
    /* 根表已私有；用户 @4GiB → VPN2=4，不碰内核 megapage 槽 */
    (void)Root;
    (void)Alloc;
    (void)Ctx;
    return 0;
}

int HalPageIsCow(UINT64 Pte) {
    return (Pte & HAL_VIEW_COW) != 0;
}

UINT64 HalPageMarkCow(UINT64 Flags) {
    return (Flags | HAL_VIEW_COW) & ~HAL_PAGE_WRITABLE;
}

int HalPageMap(UINT64 Root, UINT64 VirtualAddress, UINT64 PhysicalAddress, UINT64 Flags,
               HalPageAllocateFunction Alloc, void *Ctx) {
    UINT64 *L2 = (UINT64 *)(UINTN)(Root & ~0xFFFULL);
    UINT64 *Pte = PageWalk(L2, VirtualAddress, 1, Alloc, Ctx);
    if (!Pte) {
        return -1;
    }
    *Pte = NativeFromHal(PhysicalAddress, Flags);
    HalFlushTlb(VirtualAddress);
    return 0;
}

int HalPageUnmapRange(UINT64 Root, UINT64 Start, UINT64 End) {
    for (UINT64 Virt = Start & ~(UINT64)(PAGE_SIZE - 1); Virt < End; Virt += PAGE_SIZE) {
        UINT64 *Pte = PageLookup(Root, Virt);
        UINT64 View;
        if (!Pte) {
            continue;
        }
        View = HalViewFromNative(*Pte);
        if (!(View & HAL_PAGE_PRESENT) || !(View & HAL_PAGE_USER)) {
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
        /* megapage @ L1：Lookup 走 L0 失败；对内核恒等可再查 L1 leaf */
        UINT64 *L2 = (UINT64 *)(UINTN)(Root & ~0xFFFULL);
        UINT64 i2 = (Virt >> 30) & 0x1FF;
        UINT64 i1 = (Virt >> 21) & 0x1FF;
        UINT64 *L1;
        if (!(L2[i2] & PTE_V) || IsLeaf(L2[i2])) {
            return IsLeaf(L2[i2]) ? HalViewFromNative(L2[i2]) : 0;
        }
        L1 = (UINT64 *)(UINTN)(((L2[i2] >> PTE_PPN_SHIFT) << 12));
        if (IsLeaf(L1[i1])) {
            return HalViewFromNative(L1[i1]);
        }
        return 0;
    }
    return HalViewFromNative(*Pte);
}

UINT64 HalPageGetEntryCurrent(UINT64 Virt) {
    return HalPageGetEntry(HalGetPageTable(), Virt);
}
