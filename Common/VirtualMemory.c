/*
 * VirtualMemory.c — 虚拟内存策略层（地址空间、用户区布局、VirtualMemoryCopyFromUser）
 */
#include "VirtualMemory.h"
#include "PhysicalMemory.h"
#include "Console.h"
#include "hal.h"
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
        HalPageUnmapRange(HalPageKernelRoot(), USER_CODE_VIRT,
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
