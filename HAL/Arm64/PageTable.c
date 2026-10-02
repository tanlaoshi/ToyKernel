/*
 * PageTable.c — HAL/Arm64 页表 Hal* API（PR-S3-pagetable-1）
 */
/*
 * HAL/Arm64/PageTable.c — PR-A10：真 MMU（TTBR0 / TCR / SCTLR）+ 4K 四级页表
 * Common 仍见 HAL_PAGE_*；HalPageGetEntry 返回规范化 PTE（phys|HAL flags|COW）。
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
    __asm__ volatile(
        "dsb ishst\n"
        "tlbi vaae1is, %0\n"
        "dsb ish\n"
        "isb\n"
        :: "r"(VirtualAddress >> 12) : "memory");
}

void HalPageTableLoad(UINT64 Root) {
    gCurrentRoot = Root;
    if (!gMmuOn) {
        return;
    }
    __asm__ volatile(
        "msr ttbr0_el1, %0\n"
        "dsb ish\n"
        "tlbi vmalle1is\n"
        "dsb ish\n"
        "isb\n"
        :: "r"(Root) : "memory");
}

UINT64 HalPageTableGetCurrent(void) {
    if (gMmuOn) {
        UINT64 T;
        __asm__ volatile("mrs %0, ttbr0_el1" : "=r"(T));
        return T & ~0xFFFULL;
    }
    return gCurrentRoot ? gCurrentRoot : gKernelRoot;
}

void HalExceptionVectorsInstall(void);

void HalPagingEnable(UINT64 RootPhys) {
    UINT64 Mair;
    UINT64 Tcr;
    UINT64 Sctlr;

    HalExceptionVectorsInstall();

    /* Attr0=Normal WB，Attr1=Device-nGnRnE */
    Mair = (0xFFULL) | (0x00ULL << 8);

    /*
     * TCR_EL1：T0SZ=16（48-bit）、TG0=4K、内/外 WB WA、IPS=40-bit(2)
     */
    Tcr = (16ULL) |           /* T0SZ */
          (0ULL << 14) |      /* TG0=4K */
          (1ULL << 8) |       /* IRGN0=WBWA */
          (1ULL << 10) |      /* ORGN0=WBWA */
          (3ULL << 12) |      /* SH0=Inner Shareable */
          (2ULL << 32);       /* IPS=40 bits */

    __asm__ volatile("msr mair_el1, %0" :: "r"(Mair));
    __asm__ volatile("msr tcr_el1, %0" :: "r"(Tcr));
    __asm__ volatile("msr ttbr0_el1, %0" :: "r"(RootPhys));
    __asm__ volatile("isb");

    gCurrentRoot = RootPhys;
    gKernelRoot = RootPhys;

    __asm__ volatile("mrs %0, sctlr_el1" : "=r"(Sctlr));
    Sctlr |= (1ULL << 0);   /* M */
    Sctlr |= (1ULL << 2);   /* C */
    Sctlr |= (1ULL << 12);  /* I */
    Sctlr &= ~(1ULL << 19); /* WXN clear */
    __asm__ volatile(
        "msr sctlr_el1, %0\n"
        "isb\n"
        :: "r"(Sctlr) : "memory");

    gMmuOn = 1;
    HalSerialWrite("vmm: Arm64 MMU on\n");
    HalPagingSelfTest();
}

/* PR-A14：AP 启用与 BSP 相同的内核页表（无自测横幅） */
void HalPagingEnableAp(void) {
    UINT64 RootPhys = gKernelRoot ? gKernelRoot : gCurrentRoot;
    UINT64 Mair;
    UINT64 Tcr;
    UINT64 Sctlr;

    if (RootPhys == 0) {
        return;
    }
    HalExceptionVectorsInstall();

    Mair = (0xFFULL) | (0x00ULL << 8);
    Tcr = (16ULL) | (0ULL << 14) | (1ULL << 8) | (1ULL << 10) |
          (3ULL << 12) | (2ULL << 32);

    __asm__ volatile("msr mair_el1, %0" ::"r"(Mair));
    __asm__ volatile("msr tcr_el1, %0" ::"r"(Tcr));
    __asm__ volatile("msr ttbr0_el1, %0" ::"r"(RootPhys));
    __asm__ volatile("isb");

    gCurrentRoot = RootPhys;

    __asm__ volatile("mrs %0, sctlr_el1" : "=r"(Sctlr));
    Sctlr |= (1ULL << 0) | (1ULL << 2) | (1ULL << 12);
    Sctlr &= ~(1ULL << 19);
    __asm__ volatile("msr sctlr_el1, %0\nisb\n" ::"r"(Sctlr) : "memory");
    gMmuOn = 1;
}

/*
 * 恒等映射 0..4GiB：低 1GiB Device（UART/GIC/virtio），其余 Normal（RAM @1GiB+）。
 * IdentityMegabytes 保留接口兼容；virt 固定覆盖 4GiB。
 */
int HalPageKernelSetup(UINTN IdentityMegabytes) {
    UINT64 *L0;
    UINT64 *L1;
    UINTN GiB = 4;
    UINTN g;
    UINT64 TableFlags = DESC_VALID | DESC_TABLE | AF_BIT | ATTR_NORMAL | SH_INNER | AP_EL1_RW;

    (void)IdentityMegabytes;

    L0 = (UINT64 *)PageAllocTable();
    L1 = (UINT64 *)PageAllocTable();
    if (!L0 || !L1) {
        return -1;
    }
    /* L0[0] → L1；VA bits[38:30] 选 L1 槽（每槽 1GiB） */
    L0[0] = PagePhys(L1) | TableFlags;

    for (g = 0; g < GiB; g++) {
        UINT64 *L2 = (UINT64 *)PageAllocTable();
        UINT64 Attr = (g == 0) ? ATTR_DEVICE : ATTR_NORMAL;
        UINT64 BlockBase = DESC_VALID | DESC_BLOCK | AF_BIT | SH_INNER | AP_EL1_RW | UXN_BIT | Attr;
        UINTN b;

        if (!L2) {
            return -1;
        }
        L1[g] = PagePhys(L2) | TableFlags;
        for (b = 0; b < 512; b++) {
            UINT64 Pa = ((UINT64)g << 30) + ((UINT64)b << 21);
            L2[b] = Pa | BlockBase;
        }
    }

    gKernelRoot = PagePhys(L0);
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
    void *L0 = Alloc(Ctx);
    if (!L0) {
        return 0;
    }
    return PagePhys(L0);
}

void HalPageRootCopy(UINT64 DstRoot, UINT64 SrcRoot) {
    UINT64 *Dst = (UINT64 *)(UINTN)(DstRoot & ~0xFFFULL);
    UINT64 *Src = (UINT64 *)(UINTN)(SrcRoot & ~0xFFFULL);
    for (int i = 0; i < 512; i++) {
        Dst[i] = Src[i];
    }
}

int HalPagePrivatizeRootSlot(UINT64 Root, UINT32 Index, HalPageAllocateFunction Alloc, void *Ctx) {
    UINT64 *L0;
    UINT64 *Old;
    UINT64 *New;
    UINT64 Flags;
    int i;

    if (!Alloc || Index >= 512) {
        return -1;
    }
    L0 = (UINT64 *)(UINTN)(Root & ~0xFFFULL);
    if (!(L0[Index] & DESC_VALID)) {
        return 0;
    }
    Old = (UINT64 *)(UINTN)(L0[Index] & 0x0000FFFFFFFFF000ULL);
    New = (UINT64 *)Alloc(Ctx);
    if (!New) {
        return -1;
    }
    for (i = 0; i < 512; i++) {
        New[i] = Old[i];
    }
    Flags = L0[Index] & 0xFFFULL;
    L0[Index] = PagePhys(New) | Flags | DESC_VALID | DESC_TABLE;
    return 0;
}

int HalPagePrepareUserRoot(UINT64 Root, HalPageAllocateFunction Alloc, void *Ctx) {
    /* L0[0] 私有：用户区 @4GiB+ 写 L1[4..] 不污染内核恒等表 */
    return HalPagePrivatizeRootSlot(Root, 0, Alloc, Ctx);
}

int HalPageIsCopyOnWrite(UINT64 Pte) {
    return (Pte & HAL_PAGE_COPY_ON_WRITE) != 0;
}

UINT64 HalPageMarkCopyOnWrite(UINT64 Flags) {
    return (Flags | HAL_PAGE_COPY_ON_WRITE) & ~HAL_PAGE_WRITABLE;
}

int HalPageMap(UINT64 Root, UINT64 VirtualAddress, UINT64 PhysicalAddress, UINT64 Flags,
               HalPageAllocateFunction Alloc, void *Ctx) {
    int User = (Flags & HAL_PAGE_USER) != 0;
    UINT64 *L0 = (UINT64 *)(UINTN)(Root & ~0xFFFULL);
    UINT64 *Pte = PageWalk(L0, VirtualAddress, 1, User, Alloc, Ctx);
    UINT64 Native;
    if (!Pte) {
        return -1;
    }
    Native = (PhysicalAddress & 0x0000FFFFFFFFF000ULL) | NativeFlagsFromHal(Flags, 0);
    if (Flags & HAL_PAGE_COPY_ON_WRITE) {
        Native |= SOFT_COW;
    }
    *Pte = Native;
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
        return 0;
    }
    return HalViewFromNative(*Pte);
}

UINT64 HalPageGetEntryCurrent(UINT64 Virt) {
    return HalPageGetEntry(HalPageTableGetCurrent(), Virt);
}

void HalPatApplyWc(void) {
}
