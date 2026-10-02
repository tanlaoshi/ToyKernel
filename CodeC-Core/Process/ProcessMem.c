/*
 * ProcessMem.c — brk / mmap（PR-S-process-1）
 */
#include "Process.h"
#include "ProcessPrivate.h"
#include "Elf.h"
#include "CoreOps.h"
#include "Scheduler.h"
#include "VirtualMemory.h"
#include "Hal.h"
#include "Console.h"
#include "Debug.h"
#include "PhysicalMemory.h"
#include "Fat.h"

static UINT64 AlignUpPage(UINT64 V) {
    return (V + (UINT64)PAGE_SIZE - 1) & ~((UINT64)PAGE_SIZE - 1);
}

/*
 * PR-P3：扩展/收缩用户堆。NewBrk==0 查询当前 break。
 * 上限 USER_BRK_MAX（SO 基址），不盖栈。
 */
UINT64 ProcessBrk(UINT64 NewBrk) {
    TASK *T;
    VIRTUAL_ADDRESS_SPACE *Space;
    UINT64 Old;
    UINT64 From;
    UINT64 To;
    UINT64 Va;

    T = SchedulerCurrent();
    if (!T || !T->IsUser || !T->UserSpace) {
        return (UINT64)(INT64)-1;
    }
    if (NewBrk == 0) {
        return T->Brk;
    }
    if (NewBrk < T->BrkBase || NewBrk > USER_BRK_MAX) {
        return (UINT64)(INT64)-1;
    }

    Space = T->UserSpace;
    Old = T->Brk;

    if (NewBrk > Old) {
        From = AlignUpPage(Old);
        To = AlignUpPage(NewBrk);
        for (Va = From; Va < To; Va += PAGE_SIZE) {
            UINT64 Pte = HalPageGetEntry(Space->Root, Va);
            void *Page;

            if ((Pte & HAL_PAGE_PRESENT) && (Pte & HAL_PAGE_USER)) {
                continue;
            }
            Page = PhysicalMemoryAllocatePage();
            if (!Page) {
                return (UINT64)(INT64)-1;
            }
            {
                UINT8 *B = (UINT8 *)Page;
                UINTN i;
                for (i = 0; i < PAGE_SIZE; i++) {
                    B[i] = 0;
                }
            }
            if (VirtualMemorySpaceMapPage(Space, Va, (UINT64)(UINTN)Page,
                                          PTE_PRESENT | PTE_WRITABLE | PTE_USER) != 0) {
                PhysicalMemoryFreePage(Page);
                return (UINT64)(INT64)-1;
            }
        }
    } else if (NewBrk < Old) {
        From = AlignUpPage(NewBrk);
        To = AlignUpPage(Old);
        for (Va = From; Va < To; Va += PAGE_SIZE) {
            UINT64 Pte = HalPageGetEntry(Space->Root, Va);
            UINT64 Phys;

            if (!(Pte & HAL_PAGE_PRESENT) || !(Pte & HAL_PAGE_USER)) {
                continue;
            }
            Phys = Pte & ~0xFFFULL;
            HalPageUnmapRange(Space->Root, Va, Va + PAGE_SIZE);
            PhysicalMemoryReleasePage((void *)(UINTN)Phys);
        }
    }

    T->Brk = NewBrk;
    return NewBrk;
}

/* PROT_* / MAP_* 与 User/include/sys/mman.h 一致（教学子集） */
#define TOY_PROT_READ  0x1
#define TOY_PROT_WRITE 0x2
#define TOY_MAP_SHARED    0x01
#define TOY_MAP_PRIVATE   0x02
#define TOY_MAP_ANONYMOUS 0x20
#define TOY_MMAP_MAX_LEN  (256u * 1024u)

static void MmapRollbackPages(VIRTUAL_ADDRESS_SPACE *Space, UINT64 Base, UINT64 VaEnd) {
    UINT64 Rb;
    for (Rb = Base; Rb < VaEnd; Rb += PAGE_SIZE) {
        UINT64 P = HalPageGetEntry(Space->Root, Rb);
        if ((P & HAL_PAGE_PRESENT) && (P & HAL_PAGE_USER)) {
            HalPageUnmapRange(Space->Root, Rb, Rb + PAGE_SIZE);
            PhysicalMemoryReleasePage((void *)(UINTN)(P & ~0xFFFULL));
        }
    }
}

static void MmapZeroPage(void *Page) {
    UINT8 *B = (UINT8 *)Page;
    UINTN i;
    for (i = 0; i < PAGE_SIZE; i++) {
        B[i] = 0;
    }
}

/*
 * PR-U-mmap / PR-U-mmap2：
 * - 匿名：MAP_ANONYMOUS（可带 MAP_PRIVATE）
 * - 文件：MAP_PRIVATE + 已打开文件 fd；offset 固定 0（CRT 保证）
 * Addr 由内核在 [USER_MMAP_BASE, END) 分配；单次 ≤256KiB。
 * ABI：Flags 低 16=mmap flags；非匿名时高 16=fd。
 */
UINT64 ProcessMmap(UINT64 Len, UINT64 Prot, UINT64 Flags) {
    TASK *T;
    VIRTUAL_ADDRESS_SPACE *Space;
    UINT64 Need;
    UINT64 Base;
    UINT64 Va;
    UINT64 FlagsPte;
    UINT64 RealFlags;
    INT32 Fd;
    TASK_FD *FileFd;
    UINTN FileOff;

    T = SchedulerCurrent();
    if (!T || !T->IsUser || !T->UserSpace) {
        return (UINT64)(INT64)-1;
    }
    if (Len == 0 || Len > TOY_MMAP_MAX_LEN) {
        return (UINT64)(INT64)-1;
    }

    RealFlags = Flags & 0xFFFFu;
    Fd = -1;
    FileFd = 0;
    if ((RealFlags & TOY_MAP_ANONYMOUS) == 0) {
        if (RealFlags & TOY_MAP_SHARED) {
            return (UINT64)(INT64)-1;
        }
        if ((RealFlags & TOY_MAP_PRIVATE) == 0) {
            return (UINT64)(INT64)-1;
        }
        Fd = (INT32)((Flags >> 16) & 0xFFFFu);
        if (Fd < 0 || Fd >= MAX_FDS || !T->Fds[Fd].Used ||
            T->Fds[Fd].Kind != FD_KIND_FILE) {
            return (UINT64)(INT64)-1;
        }
        FileFd = &T->Fds[Fd];
    } else if (RealFlags & TOY_MAP_SHARED) {
        return (UINT64)(INT64)-1;
    }

    if ((Prot & (TOY_PROT_READ | TOY_PROT_WRITE)) == 0) {
        return (UINT64)(INT64)-1;
    }

    Need = AlignUpPage(Len);
    if (T->MmapNext < USER_MMAP_BASE) {
        T->MmapNext = USER_MMAP_BASE;
    }
    Base = AlignUpPage(T->MmapNext);
    if (Base < USER_MMAP_BASE || Base > USER_MMAP_END ||
        Base + Need < Base || Base + Need > USER_MMAP_END) {
        return (UINT64)(INT64)-1;
    }

    Space = T->UserSpace;
    FlagsPte = PTE_PRESENT | PTE_USER;
    if (Prot & TOY_PROT_WRITE) {
        FlagsPte |= PTE_WRITABLE;
    }

    FileOff = 0;
    for (Va = Base; Va < Base + Need; Va += PAGE_SIZE) {
        UINT64 Pte = HalPageGetEntry(Space->Root, Va);
        void *Page;

        if ((Pte & HAL_PAGE_PRESENT) && (Pte & HAL_PAGE_USER)) {
            return (UINT64)(INT64)-1;
        }
        Page = PhysicalMemoryAllocatePage();
        if (!Page) {
            MmapRollbackPages(Space, Base, Va);
            return (UINT64)(INT64)-1;
        }
        MmapZeroPage(Page);
        if (FileFd) {
            if (FileOff < FileFd->Size) {
                UINTN N = FileFd->Size - FileOff;
                UINTN Got = 0;
                if (N > PAGE_SIZE) {
                    N = PAGE_SIZE;
                }
                /*
                 * PR-TEST：流式文件无 Data 缓冲（SchedulerFdOpen 设 Data=0），
                 * 直接从盘按偏移读入新页。旧代码读 FileFd->Data[off] 永远失败。
                 */
                if (VfsServiceReadFileAt(FileFd->Path, FileOff, Page, N, &Got) != FAT_OK) {
                    PhysicalMemoryFreePage(Page);
                    MmapRollbackPages(Space, Base, Va);
                    return (UINT64)(INT64)-1;
                }
            }
            FileOff += PAGE_SIZE;
        }
        if (VirtualMemorySpaceMapPage(Space, Va, (UINT64)(UINTN)Page, FlagsPte) != 0) {
            PhysicalMemoryFreePage(Page);
            MmapRollbackPages(Space, Base, Va);
            return (UINT64)(INT64)-1;
        }
    }

    T->MmapNext = Base + Need;
    return Base;
}

UINT64 ProcessMunmap(UINT64 Addr, UINT64 Len) {
    TASK *T;
    VIRTUAL_ADDRESS_SPACE *Space;
    UINT64 From;
    UINT64 To;
    UINT64 Va;

    T = SchedulerCurrent();
    if (!T || !T->IsUser || !T->UserSpace) {
        return (UINT64)(INT64)-1;
    }
    if (Len == 0 || (Addr & ((UINT64)PAGE_SIZE - 1)) != 0) {
        return (UINT64)(INT64)-1;
    }
    if (Addr < USER_MMAP_BASE || Addr >= USER_MMAP_END) {
        return (UINT64)(INT64)-1;
    }
    From = Addr;
    To = AlignUpPage(Addr + Len);
    if (To < From || To > USER_MMAP_END) {
        return (UINT64)(INT64)-1;
    }

    Space = T->UserSpace;
    for (Va = From; Va < To; Va += PAGE_SIZE) {
        UINT64 Pte = HalPageGetEntry(Space->Root, Va);
        UINT64 Phys;

        if (!(Pte & HAL_PAGE_PRESENT) || !(Pte & HAL_PAGE_USER)) {
            continue;
        }
        Phys = Pte & ~0xFFFULL;
        HalPageUnmapRange(Space->Root, Va, Va + PAGE_SIZE);
        PhysicalMemoryReleasePage((void *)(UINTN)Phys);
    }
    return 0;
}
