/*
 * PageTable.c — HAL/RiscV 页表 Hal* API（PR-S3-pagetable-1）
 */
/*
 * HAL/RiscV/PageTable.c — PR-A10：真 MMU（Sv39 satp）+ 缺页陷阱
 * Common 仍见 HAL_PAGE_*；GetEntry 返回规范化 PTE。
 */
#include "Hal.h"
#include "PhysicalMemory.h"

#include "PageTablePrivate.h"

UINT64 gKernelRoot;
UINT64 gCurrentRoot;
int gMmuOn;


void HalTlbFlush(UINT64 VirtualAddress) {
    if (!gMmuOn) {
        return;
    }
    __asm__ volatile("sfence.vma %0, zero" :: "r"(VirtualAddress) : "memory");
}

void HalPageTableLoad(UINT64 Root) {
    gCurrentRoot = Root;
    if (!gMmuOn) {
        return;
    }
    __asm__ volatile(
        "csrw satp, %0\n"
        "sfence.vma\n"
        :: "r"(SATP_MODE_SV39 | (Root >> 12)) : "memory");
}

UINT64 HalPageTableGetCurrent(void) {
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

/* PR-A14：AP 装载与 BSP 相同的 Sv39 根 */
void HalPagingEnableAp(void) {
    UINT64 RootPhys = gKernelRoot ? gKernelRoot : gCurrentRoot;

    if (RootPhys == 0) {
        return;
    }
    HalTrapVectorInstall();
    gCurrentRoot = RootPhys;
    __asm__ volatile(
        "csrw satp, %0\n"
        "sfence.vma\n" ::"r"(SATP_MODE_SV39 | (RootPhys >> 12))
        : "memory");
    gMmuOn = 1;
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

int HalPageIsCopyOnWrite(UINT64 Pte) {
    return (Pte & HAL_PAGE_COPY_ON_WRITE) != 0;
}

UINT64 HalPageMarkCopyOnWrite(UINT64 Flags) {
    return (Flags | HAL_PAGE_COPY_ON_WRITE) & ~HAL_PAGE_WRITABLE;
}

int HalPageMap(UINT64 Root, UINT64 VirtualAddress, UINT64 PhysicalAddress, UINT64 Flags,
               HalPageAllocateFunction Alloc, void *Ctx) {
    UINT64 *L2 = (UINT64 *)(UINTN)(Root & ~0xFFFULL);
    UINT64 *Pte = PageWalk(L2, VirtualAddress, 1, Alloc, Ctx);
    if (!Pte) {
        return -1;
    }
    *Pte = NativeFromHal(PhysicalAddress, Flags);
    HalTlbFlush(VirtualAddress);
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
        HalTlbFlush(Virt);
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
    return HalPageGetEntry(HalPageTableGetCurrent(), Virt);
}

void HalPatApplyWc(void) {
}
