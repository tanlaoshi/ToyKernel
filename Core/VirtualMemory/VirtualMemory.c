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

void *VirtualMemorySpaceAllocateAndTrack(VIRTUAL_ADDRESS_SPACE *Space) {
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

VIRTUAL_ADDRESS_SPACE *VirtualMemorySpaceCreate(void) {
    VIRTUAL_ADDRESS_SPACE *Space = (VIRTUAL_ADDRESS_SPACE *)PhysicalMemoryAllocatePage();
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

UINT64 VirtualMemorySpaceRoot(const VIRTUAL_ADDRESS_SPACE *Space) {
    if (!Space) {
        return 0;
    }
    return Space->Root;
}

int VirtualMemorySpaceMapPage(VIRTUAL_ADDRESS_SPACE *Space, UINT64 Virt, UINT64 Phys, UINT64 Flags) {
    if (!Space) {
        return -1;
    }
    return HalPageMap(Space->Root, Virt, Phys, Flags,
                      (HalPageAllocateFunction)VirtualMemorySpaceAllocateAndTrack, Space);
}

void VirtualMemorySpaceDestroy(VIRTUAL_ADDRESS_SPACE *Space) {
    UINT64 Va;
    void *Tracked[VM_SPACE_MAX_PAGES];
    int Count;
    int i;

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
            Phys = Pte & 0x000FFFFFFFFFF000ULL;
            PhysicalMemoryReleasePage((void *)(UINTN)Phys);
        }
        HalPageUnmapRange(Space->Root, USER_CODE_VIRT, USER_VIRT_END);
    }
    /*
     * Pages[0] 即 Space 自身。若边读 Space->Pages[] 边 Free，
     * 第一次 Free 后就是 UAF：会误释其它物理页 → 二次 exec 页表 RSVD/#PF@0x40000000。
     */
    Count = Space->PageCount;
    if (Count < 0) {
        Count = 0;
    }
    if (Count > VM_SPACE_MAX_PAGES) {
        Count = VM_SPACE_MAX_PAGES;
    }
    for (i = 0; i < Count; i++) {
        Tracked[i] = Space->Pages[i];
    }
    for (i = 0; i < Count; i++) {
        if (Tracked[i] != 0) {
            PhysicalMemoryFreePage(Tracked[i]);
        }
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
            if (HalPageIsCopyOnWrite(Pte) && !(Pte & HAL_PAGE_WRITABLE)) {
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

int VirtualMemoryCopyToSpace(VIRTUAL_ADDRESS_SPACE *Space, UINT64 UserDst, const void *Src,
                             UINTN Len) {
    const UINT8 *S = (const UINT8 *)Src;
    UINTN i;

    if (!Space || !Src) {
        return -1;
    }
    if (Len == 0) {
        return 0;
    }
    if (UserDst < USER_CODE_VIRT || UserDst + Len > USER_VIRT_END ||
        UserDst + Len < UserDst) {
        return -1;
    }

    for (i = 0; i < Len; i++) {
        UINT64 Va = UserDst + i;
        UINT64 Pte = HalPageGetEntry(Space->Root, Va);
        UINT64 Phys;
        UINT8 *Dst;

        if (!(Pte & HAL_PAGE_PRESENT) || !(Pte & HAL_PAGE_USER)) {
            return -1;
        }
        if (!(Pte & HAL_PAGE_WRITABLE)) {
            /* exec 装栈/重定位不应碰到只读/未拆 COW；失败即可 */
            return -1;
        }
        Phys = Pte & 0x000FFFFFFFFFF000ULL;
        Dst = (UINT8 *)(UINTN)(Phys + (Va & (PAGE_SIZE - 1)));
        *Dst = S[i];
    }
    return (int)Len;
}

int VirtualMemoryInitialize(void) {
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

static int gVirtualMemoryEnabled;

void VirtualMemoryEnable(void) {
    HalPagingEnable(HalPageKernelRoot());
    gVirtualMemoryEnabled = 1;
    DebugWrite("VMM: paging enabled\n");
}

int VirtualMemoryEnabled(void) {
    return gVirtualMemoryEnabled;
}

UINT64 VirtualMemoryKernelRoot(void) {
    return HalPageKernelRoot();
}
