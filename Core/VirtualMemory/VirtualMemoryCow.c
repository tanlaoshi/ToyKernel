/*
 * VirtualMemoryCow.c — 写时复制：克隆与缺页（PR-S-virtualmemory-1）
 */
#include "VirtualMemory.h"
#include "PhysicalMemory.h"
#include "Console.h"
#include "Hal.h"
#include "Debug.h"
/*
 * COW fork：共享用户物理页；原可写页双方去掉 W、打上 COW（HalPageMarkCopyOnWrite）。
 * 页表仍私有（SpaceCreate 已 HalPagePrepareUserRoot）。
 */
VIRTUAL_ADDRESS_SPACE *VirtualMemorySpaceClone(VIRTUAL_ADDRESS_SPACE *Src) {
    VIRTUAL_ADDRESS_SPACE *Dst;
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
            SharedFlags = HalPageMarkCopyOnWrite(SharedFlags);
            if (HalPageMap(Src->Root, Va, Phys, SharedFlags, 0, 0) != 0) {
                VirtualMemorySpaceDestroy(Dst);
                return 0;
            }
        } else if (HalPageIsCopyOnWrite(Pte)) {
            SharedFlags = HalPageMarkCopyOnWrite(SharedFlags);
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
    Root = HalGetCurrentPageTable();
    Pte = HalPageGetEntry(Root, Va);
    if (!(Pte & HAL_PAGE_PRESENT) || !(Pte & HAL_PAGE_USER) || !HalPageIsCopyOnWrite(Pte)) {
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
