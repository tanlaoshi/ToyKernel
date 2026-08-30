/*
 * Vmm.c — 虚拟内存策略层（地址空间、用户区布局、CopyFromUser）
 *
 * x86 四级页表遍历与映射在 hal/x86_64/Page.c。
 */
#include "Vmm.h"
#include "Pmm.h"
#include "Console.h"
#include "hal.h"

#define IDENTITY_MB 512

static void VmmZero(void *Ptr, UINTN Size) {
    UINT8 *B = (UINT8 *)Ptr;
    for (UINTN i = 0; i < Size; i++) {
        B[i] = 0;
    }
}

void *VmmSpaceAllocAndTrack(VM_ADDR_SPACE *Space) {
    void *Page = PmmAllocPage();
    if (!Page || !Space) {
        return 0;
    }
    if (Space->PageCount >= VM_SPACE_MAX_PAGES) {
        PmmFreePage(Page);
        return 0;
    }
    Space->Pages[Space->PageCount++] = Page;
    VmmZero(Page, PAGE_SIZE);
    return Page;
}

int VmmMapPage(UINT64 Virt, UINT64 Phys, UINT64 Flags) {
    return HalPageMap(HalPageKernelRoot(), Virt, Phys, Flags, 0, 0);
}

int VmmMapRange(UINT64 Virt, UINT64 Phys, UINTN Bytes, UINT64 Flags) {
    for (UINTN Off = 0; Off < Bytes; Off += PAGE_SIZE) {
        if (VmmMapPage(Virt + Off, Phys + Off, Flags) != 0) {
            return -1;
        }
    }
    return 0;
}

VM_ADDR_SPACE *VmmSpaceCreate(void) {
    VM_ADDR_SPACE *Space = (VM_ADDR_SPACE *)PmmAllocPage();
    if (!Space) {
        return 0;
    }
    VmmZero(Space, sizeof(*Space));
    Space->Pages[Space->PageCount++] = Space;

    Space->Root = HalPageRootCreate((HalPageAllocFn)VmmSpaceAllocAndTrack, Space);
    if (Space->Root == 0) {
        VmmSpaceDestroy(Space);
        return 0;
    }

    HalPageRootCopy(Space->Root, HalPageKernelRoot());
    return Space;
}

UINT64 VmmSpaceCr3(const VM_ADDR_SPACE *Space) {
    if (!Space) {
        return 0;
    }
    return Space->Root;
}

int VmmSpaceMapPage(VM_ADDR_SPACE *Space, UINT64 Virt, UINT64 Phys, UINT64 Flags) {
    if (!Space) {
        return -1;
    }
    return HalPageMap(Space->Root, Virt, Phys, Flags,
                      (HalPageAllocFn)VmmSpaceAllocAndTrack, Space);
}

void VmmSpaceDestroy(VM_ADDR_SPACE *Space) {
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
        PmmFreePage(Space->Pages[i]);
    }
}

void VmmLoadCr3(UINT64 Cr3) {
    HalLoadPageTable(Cr3);
}

int VmmUserAccessOk(UINT64 Virt, UINTN Len) {
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

int CopyFromUser(void *Dst, UINT64 UserSrc, UINTN Len) {
    if (!Dst || !VmmUserAccessOk(UserSrc, Len)) {
        return -1;
    }
    UINT8 *D = (UINT8 *)Dst;
    for (UINTN i = 0; i < Len; i++) {
        D[i] = *(volatile UINT8 *)(UINTN)(UserSrc + i);
    }
    return (int)Len;
}

int VmmInit(void) {
    if (HalPageKernelSetup(IDENTITY_MB) != 0) {
        return -1;
    }

    ConsoleWrite("VMM: identity map 0-");
    ConsoleHex32(IDENTITY_MB);
    ConsoleWrite("MB (2M pages), PML4=");
    ConsoleHex64(HalPageKernelRoot());
    ConsoleWrite("\n");
    return 0;
}

void VmmEnable(void) {
    HalPagingEnable(HalPageKernelRoot());
    ConsoleWrite("VMM: paging enabled\n");
}

UINT64 VmmKernelCr3(void) {
    return HalPageKernelRoot();
}
