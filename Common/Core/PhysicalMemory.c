/*
 * PhysicalMemory.c — 物理页分配器（位图实现）
 */
#include "PhysicalMemory.h"
#include "BootInfo.h"
#include "Console.h"
#include "Debug.h"

#define PHYSICAL_MEMORY_MAX_PAGES (128u * 1024u)

static UINT8  gBitmap[PHYSICAL_MEMORY_MAX_PAGES / 8];
static UINT16 gRefCount[PHYSICAL_MEMORY_MAX_PAGES];
static UINT32 gMaxPage;
static UINT64 gFreePages;

static int PageUsed(UINT32 Pfn) {
    if (Pfn >= gMaxPage) {
        return 1;
    }
    return (gBitmap[Pfn / 8] >> (Pfn % 8)) & 1;
}

static void SetPage(UINT32 Pfn, int Used) {
    if (Pfn >= gMaxPage) {
        return;
    }
    if (Used) {
        if (!PageUsed(Pfn)) {
            gFreePages--;
        }
        gBitmap[Pfn / 8] |= (UINT8)(1u << (Pfn % 8));
    } else {
        if (PageUsed(Pfn)) {
            gFreePages++;
        }
        gBitmap[Pfn / 8] &= (UINT8)~(1u << (Pfn % 8));
    }
}

static void ReserveRange(UINT64 Phys, UINT64 Size) {
    if (Size == 0) {
        return;
    }
    UINT64 Start = Phys & ~(UINT64)(PAGE_SIZE - 1);
    UINT64 End = Phys + Size;
    while (Start < End) {
        SetPage((UINT32)(Start >> PAGE_SHIFT), 1);
        Start += PAGE_SIZE;
    }
}

static void AddFreeRange(UINT64 Phys, UINT64 Size) {
    UINT64 End = Phys + Size;
    while (Phys < End) {
        UINT32 Pfn = (UINT32)(Phys >> PAGE_SHIFT);
        if (Pfn < gMaxPage) {
            SetPage(Pfn, 0);
        }
        Phys += PAGE_SIZE;
    }
}

static UINT32 ComputeMaxPage(const BOOT_INFO *Info) {
    UINT32 Max = 0;
    UINT32 i;

    for (i = 0; i < Info->RegionCount; i++) {
        UINT64 End = Info->Regions[i].Phys + Info->Regions[i].Size;
        UINT32 Pfn = (UINT32)(End >> PAGE_SHIFT);
        if (Pfn > Max) {
            Max = Pfn;
        }
    }
    if (Max > PHYSICAL_MEMORY_MAX_PAGES) {
        Max = PHYSICAL_MEMORY_MAX_PAGES;
    }
    return Max;
}

int PhysicalMemoryInit(void) {
    const BOOT_INFO *Info = BootInfoGet();
    UINT32 i;

    if (!Info || Info->RegionCount == 0) {
        return -1;
    }

    gMaxPage = ComputeMaxPage(Info);
    if (gMaxPage == 0) {
        return -1;
    }

    for (i = 0; i < (PHYSICAL_MEMORY_MAX_PAGES / 8); i++) {
        gBitmap[i] = 0xFF;
    }
    for (i = 0; i < PHYSICAL_MEMORY_MAX_PAGES; i++) {
        gRefCount[i] = 0;
    }
    gFreePages = 0;

    for (i = 0; i < Info->RegionCount; i++) {
        const BOOT_MEMORY_REGION *R = &Info->Regions[i];
        if (R->Free) {
            AddFreeRange(R->Phys, R->Size);
        } else {
            ReserveRange(R->Phys, R->Size);
        }
    }

    DebugWrite("PMM: ");
    DebugHex64(gFreePages << PAGE_SHIFT);
    DebugWrite(" bytes free / ");
    DebugHex64((UINT64)gMaxPage << PAGE_SHIFT);
    DebugWrite(" bytes tracked (");
    DebugHex32((UINT32)gFreePages);
    DebugWrite(" pages free)\n");
    return 0;
}

void *PhysicalMemoryAllocatePages(UINT32 Count) {
    if (Count == 0 || Count > gMaxPage) {
        return 0;
    }

    UINT32 Run = 0;
    for (UINT32 Pfn = 0; Pfn < gMaxPage; Pfn++) {
        if (!PageUsed(Pfn)) {
            Run++;
            if (Run == Count) {
                UINT32 Start = Pfn + 1 - Count;
                for (UINT32 i = 0; i < Count; i++) {
                    SetPage(Start + i, 1);
                    gRefCount[Start + i] = 1;
                }
                return (void *)(UINTN)(Start << PAGE_SHIFT);
            }
        } else {
            Run = 0;
        }
    }
    return 0;
}

void *PhysicalMemoryAllocatePage(void) {
    return PhysicalMemoryAllocatePages(1);
}

int PhysicalMemoryRetainPage(void *Page) {
    UINT64 Phys = (UINT64)(UINTN)Page;
    UINT32 Pfn;

    if ((Phys & (PAGE_SIZE - 1)) != 0) {
        return -1;
    }
    Pfn = (UINT32)(Phys >> PAGE_SHIFT);
    if (Pfn >= gMaxPage || gRefCount[Pfn] == 0) {
        return -1;
    }
    if (gRefCount[Pfn] == 0xFFFF) {
        return -1;
    }
    gRefCount[Pfn]++;
    return 0;
}

void PhysicalMemoryReleasePage(void *Page) {
    UINT64 Phys = (UINT64)(UINTN)Page;
    UINT32 Pfn;

    if ((Phys & (PAGE_SIZE - 1)) != 0) {
        return;
    }
    Pfn = (UINT32)(Phys >> PAGE_SHIFT);
    if (Pfn >= gMaxPage || gRefCount[Pfn] == 0) {
        return;
    }
    gRefCount[Pfn]--;
    if (gRefCount[Pfn] == 0) {
        SetPage(Pfn, 0);
    }
}

void PhysicalMemoryFreePages(void *Page, UINT32 Count) {
    UINT64 Phys = (UINT64)(UINTN)Page;
    UINT32 Pfn;
    UINT32 i;

    if ((Phys & (PAGE_SIZE - 1)) != 0) {
        return;
    }
    Pfn = (UINT32)(Phys >> PAGE_SHIFT);
    for (i = 0; i < Count; i++) {
        if (Pfn + i >= gMaxPage) {
            return;
        }
        PhysicalMemoryReleasePage((void *)(UINTN)((UINT64)(Pfn + i) << PAGE_SHIFT));
    }
}

void PhysicalMemoryFreePage(void *Page) {
    PhysicalMemoryReleasePage(Page);
}

UINT64 PhysicalMemoryTotalPages(void) {
    return gMaxPage;
}

UINT64 PhysicalMemoryFreePageCount(void) {
    return gFreePages;
}
