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
    /* 用户区 0x40000000 与内核恒等映射同属 PML4[0]；必须私有 PDPT 才能安全 fork */
    if (HalPagePrivatizePml4Slot(Space->Root, 0,
                                 (HalPageAllocateFunction)VirtualMemorySpaceAllocateAndTrack,
                                 Space) != 0) {
        VirtualMemorySpaceDestroy(Space);
        return 0;
    }
    return Space;
}

UINT64 VirtualMemorySpaceCr3(const VM_ADDR_SPACE *Space) {
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
    if (!Space) {
        return;
    }
    if (Space->Root != 0) {
        HalPageUnmapRange(Space->Root, USER_CODE_VIRT,
                          USER_STACK_VIRT + USER_STACK_SIZE);
    }
    for (int i = 0; i < Space->PageCount; i++) {
        PhysicalMemoryFreePage(Space->Pages[i]);
    }
}

void VirtualMemoryLoadCr3(UINT64 Cr3) {
    HalLoadPageTable(Cr3);
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
    const UINT8 *S = (const UINT8 *)Src;
    for (UINTN i = 0; i < Len; i++) {
        *(volatile UINT8 *)(UINTN)(UserDst + i) = S[i];
    }
    return (int)Len;
}

/*
 * 深拷贝用户区页（代码+栈）。页表项经 HalPageGetEntry 读取，内容按物理恒等映射复制。
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
        UINT64 Flags;
        UINT64 Phys;
        void *NewPage;
        UINT8 *From;
        UINT8 *To;
        UINTN i;

        if (!(Pte & HAL_PAGE_PRESENT) || !(Pte & HAL_PAGE_USER)) {
            continue;
        }
        Flags = PTE_PRESENT | PTE_USER;
        if (Pte & HAL_PAGE_WRITABLE) {
            Flags |= PTE_WRITABLE;
        }
        Phys = Pte & ~0xFFFULL;
        NewPage = VirtualMemorySpaceAllocateAndTrack(Dst);
        if (!NewPage) {
            VirtualMemorySpaceDestroy(Dst);
            return 0;
        }
        From = (UINT8 *)(UINTN)Phys;
        To = (UINT8 *)NewPage;
        for (i = 0; i < PAGE_SIZE; i++) {
            To[i] = From[i];
        }
        if (VirtualMemorySpaceMapPage(Dst, Va, (UINT64)(UINTN)NewPage, Flags) != 0) {
            VirtualMemorySpaceDestroy(Dst);
            return 0;
        }
    }
    return Dst;
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

UINT64 VirtualMemoryKernelCr3(void) {
    return HalPageKernelRoot();
}
