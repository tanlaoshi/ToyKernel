/*
 * VirtualMemory.c — 虚拟内存策略层（地址空间、用户区布局、VirtualMemoryCopyFromUser）
 */
#include "VirtualMemory.h"
#include "PhysicalMemory.h"
#include "Console.h"
#include "Hal.h"
#include "Debug.h"

#define IDENTITY_MB 512

static void VirtualMemoryZero(void *Ptr, UINTN Size) {
    UINT8 *B = (UINT8 *)Ptr;
    for (UINTN i = 0; i < Size; i++) {
        B[i] = 0;
    }
}

void *VirtualMemorySpaceAllocateAndTrack(VM_ADDR_SPACE *Space) {
    void *Page = PhysicalMemoryAllocatePage();
    if (!Page || !Space) {
        return 0;
    }
    if (Space->PageCount >= VM_SPACE_MAX_PAGES) {
        PhysicalMemoryFreePage(Page);
        return 0;
    }
    Space->Pages[Space->PageCount++] = Page;
    VirtualMemoryZero(Page, PAGE_SIZE);
    return Page;
}

int VirtualMemoryMapPage(UINT64 Virt, UINT64 Phys, UINT64 Flags) {
    return HalPageMap(HalPageKernelRoot(), Virt, Phys, Flags, 0, 0);
}

int VirtualMemoryMapRange(UINT64 Virt, UINT64 Phys, UINTN Bytes, UINT64 Flags) {
    for (UINTN Off = 0; Off < Bytes; Off += PAGE_SIZE) {
        if (VirtualMemoryMapPage(Virt + Off, Phys + Off, Flags) != 0) {
            return -1;
        }
    }
    return 0;
}

VM_ADDR_SPACE *VirtualMemorySpaceCreate(void) {
    VM_ADDR_SPACE *Space = (VM_ADDR_SPACE *)PhysicalMemoryAllocatePage();
    if (!Space) {
        return 0;
    }
    VirtualMemoryZero(Space, sizeof(*Space));
    Space->Pages[Space->PageCount++] = Space;

    Space->Root = HalPageRootCreate((HalPageAllocateFunction)VirtualMemorySpaceAllocateAndTrack, Space);
    if (Space->Root == 0) {
        VirtualMemorySpaceDestroy(Space);
        return 0;
    }

    HalPageRootCopy(Space->Root, HalPageKernelRoot());
    /* 用户区与内核恒等映射可能共享根槽；由 HAL 决定如何私有化 */
    if (HalPagePrepareUserRoot(Space->Root,
                               (HalPageAllocateFunction)VirtualMemorySpaceAllocateAndTrack,
                               Space) != 0) {
        VirtualMemorySpaceDestroy(Space);
        return 0;
    }
    return Space;
}

UINT64 VirtualMemorySpaceRoot(const VM_ADDR_SPACE *Space) {
    if (!Space) {
        return 0;
    }
    return Space->Root;
}

int VirtualMemorySpaceMapPage(VM_ADDR_SPACE *Space, UINT64 Virt, UINT64 Phys, UINT64 Flags) {
    if (!Space) {
        return -1;
    }
    return HalPageMap(Space->Root, Virt, Phys, Flags,
                      (HalPageAllocateFunction)VirtualMemorySpaceAllocateAndTrack, Space);
}

void VirtualMemorySpaceDestroy(VM_ADDR_SPACE *Space) {
    UINT64 Va;

    if (!Space) {
        return;
    }
    if (Space->Root != 0) {
        for (Va = USER_CODE_VIRT; Va < USER_VIRT_END; Va += PAGE_SIZE) {
            UINT64 Pte = HalPageGetEntry(Space->Root, Va);
            UINT64 Phys;

            if (!(Pte & HAL_PAGE_PRESENT) || !(Pte & HAL_PAGE_USER)) {
                continue;
            }
            Phys = Pte & ~0xFFFULL;
            PhysicalMemoryReleasePage((void *)(UINTN)Phys);
        }
        HalPageUnmapRange(Space->Root, USER_CODE_VIRT,
                          USER_STACK_VIRT + USER_STACK_SIZE);
    }
    for (int i = 0; i < Space->PageCount; i++) {
        PhysicalMemoryFreePage(Space->Pages[i]);
    }
}

void VirtualMemoryLoadPageTable(UINT64 Root) {
    HalLoadPageTable(Root);
}

int VirtualMemoryUserAccessOk(UINT64 Virt, UINTN Len) {
    if (Len == 0) {
        return 1;
    }
    if (Virt < USER_CODE_VIRT || Virt + Len > USER_VIRT_END || Virt + Len < Virt) {
        return 0;
    }
    UINT64 Start = Virt & ~(UINT64)(PAGE_SIZE - 1);
    UINT64 End = Virt + Len - 1;
    for (UINT64 Va = Start; Va <= End; Va += PAGE_SIZE) {
        UINT64 Pte = HalPageGetEntryCurrent(Va);
        if (!(Pte & HAL_PAGE_PRESENT) || !(Pte & HAL_PAGE_USER)) {
            return 0;
        }
    }
    return 1;
}

int VirtualMemoryCopyFromUser(void *Dst, UINT64 UserSrc, UINTN Len) {
    if (!Dst || !VirtualMemoryUserAccessOk(UserSrc, Len)) {
        return -1;
    }
    UINT8 *D = (UINT8 *)Dst;
    for (UINTN i = 0; i < Len; i++) {
        D[i] = *(volatile UINT8 *)(UINTN)(UserSrc + i);
    }
    return (int)Len;
}

int VirtualMemoryCopyToUser(UINT64 UserDst, const void *Src, UINTN Len) {
    if (!Src || !VirtualMemoryUserAccessOk(UserDst, Len)) {
        return -1;
    }
    {
        UINT64 Start = UserDst & ~(UINT64)(PAGE_SIZE - 1);
        UINT64 End = UserDst + Len - 1;
        for (UINT64 Va = Start; Va <= End; Va += PAGE_SIZE) {
            UINT64 Pte = HalPageGetEntryCurrent(Va);
            if (HalPageIsCow(Pte) && !(Pte & HAL_PAGE_WRITABLE)) {
                if (VirtualMemoryHandlePageFault(Va, 0x7) != 0) {
                    return -1;
                }
            }
        }
    }
    const UINT8 *S = (const UINT8 *)Src;
    for (UINTN i = 0; i < Len; i++) {
        *(volatile UINT8 *)(UINTN)(UserDst + i) = S[i];
    }
    return (int)Len;
}

/*
 * COW fork：共享用户物理页；原可写页双方去掉 W、打上 COW（HalPageMarkCow）。
 * 页表仍私有（SpaceCreate 已 HalPagePrepareUserRoot）。
 */
VM_ADDR_SPACE *VirtualMemorySpaceClone(VM_ADDR_SPACE *Src) {
    VM_ADDR_SPACE *Dst;
    UINT64 Va;

    if (!Src || Src->Root == 0) {
        return 0;
    }
    Dst = VirtualMemorySpaceCreate();
    if (!Dst) {
        return 0;
    }

    for (Va = USER_CODE_VIRT; Va < USER_VIRT_END; Va += PAGE_SIZE) {
        UINT64 Pte = HalPageGetEntry(Src->Root, Va);
        UINT64 Phys;
        UINT64 SharedFlags;

        if (!(Pte & HAL_PAGE_PRESENT) || !(Pte & HAL_PAGE_USER)) {
            continue;
        }
        Phys = Pte & ~0xFFFULL;
        SharedFlags = PTE_PRESENT | PTE_USER;
        if (Pte & HAL_PAGE_WRITABLE) {
            SharedFlags = HalPageMarkCow(SharedFlags);
            if (HalPageMap(Src->Root, Va, Phys, SharedFlags, 0, 0) != 0) {
                VirtualMemorySpaceDestroy(Dst);
                return 0;
            }
        } else if (HalPageIsCow(Pte)) {
            SharedFlags = HalPageMarkCow(SharedFlags);
        }

        if (PhysicalMemoryRetainPage((void *)(UINTN)Phys) != 0) {
            VirtualMemorySpaceDestroy(Dst);
            return 0;
        }
        if (VirtualMemorySpaceMapPage(Dst, Va, Phys, SharedFlags) != 0) {
            PhysicalMemoryReleasePage((void *)(UINTN)Phys);
            VirtualMemorySpaceDestroy(Dst);
            return 0;
        }
    }
    return Dst;
}

int VirtualMemoryHandlePageFault(UINT64 FaultAddress, UINT64 ErrorCode) {
    UINT64 Va;
    UINT64 Pte;
    UINT64 Phys;
    void *NewPage;
    UINT8 *From;
    UINT8 *To;
    UINTN i;
    UINT64 Root;

    /* bit0=P present, bit1=W write, bit2=U user */
    if ((ErrorCode & 0x7) != 0x7) {
        return -1;
    }
    Va = FaultAddress & ~(UINT64)(PAGE_SIZE - 1);
    if (Va < USER_CODE_VIRT || Va >= USER_VIRT_END) {
        return -1;
    }
    Root = HalGetPageTable();
    Pte = HalPageGetEntry(Root, Va);
    if (!(Pte & HAL_PAGE_PRESENT) || !(Pte & HAL_PAGE_USER) || !HalPageIsCow(Pte)) {
        return -1;
    }
    if (Pte & HAL_PAGE_WRITABLE) {
        return -1;
    }

    Phys = Pte & ~0xFFFULL;
    NewPage = PhysicalMemoryAllocatePage();
    if (!NewPage) {
        return -1;
    }
    From = (UINT8 *)(UINTN)Phys;
    To = (UINT8 *)NewPage;
    for (i = 0; i < PAGE_SIZE; i++) {
        To[i] = From[i];
    }
    if (HalPageMap(Root, Va, (UINT64)(UINTN)NewPage,
                   PTE_PRESENT | PTE_USER | PTE_WRITABLE, 0, 0) != 0) {
        PhysicalMemoryFreePage(NewPage);
        return -1;
    }
    PhysicalMemoryReleasePage((void *)(UINTN)Phys);
    return 0;
}

int VirtualMemoryInit(void) {
    if (HalPageKernelSetup(IDENTITY_MB) != 0) {
        return -1;
    }

    DebugWrite("VMM: identity map 0-");
    DebugHex32(IDENTITY_MB);
    DebugWrite("MB (2M pages), PML4=");
    DebugHex64(HalPageKernelRoot());
    DebugWrite("\n");
    return 0;
}

void VirtualMemoryEnable(void) {
    HalPagingEnable(HalPageKernelRoot());
    DebugWrite("VMM: paging enabled\n");
}

UINT64 VirtualMemoryKernelRoot(void) {
    return HalPageKernelRoot();
}
