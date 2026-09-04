/*
 * PhysicalMemory.c — 物理页分配器（位图；PR-A7：PhysBase 支持高位 RAM）
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
static UINT64 gPhysBase; /* 页对齐；PFN 0 对应此物理地址（x86 常为 0） */

static UINT32 PhysToPfn(UINT64 Phys) {
    if (Phys < gPhysBase) {
        return gMaxPage;
    }
    return (UINT32)((Phys - gPhysBase) >> PAGE_SHIFT);
}

static UINT64 PfnToPhys(UINT32 Pfn) {
    return gPhysBase + ((UINT64)Pfn << PAGE_SHIFT);
}

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
        SetPage(PhysToPfn(Start), 1);
        Start += PAGE_SIZE;
    }
}

static void AddFreeRange(UINT64 Phys, UINT64 Size) {
    UINT64 End = Phys + Size;
    while (Phys < End) {
        UINT32 Pfn = PhysToPfn(Phys);
        if (Pfn < gMaxPage) {
            SetPage(Pfn, 0);
        }
        Phys += PAGE_SIZE;
    }
}

static void ComputeBaseAndMax(const BOOT_INFO *Info, UINT64 *OutBase, UINT32 *OutMax) {
    UINT64 Base = ~(UINT64)0;
    UINT64 EndMax = 0;
    UINT32 i;
    UINT32 Span;

    for (i = 0; i < Info->RegionCount; i++) {
        UINT64 P = Info->Regions[i].Phys;
        UINT64 E = P + Info->Regions[i].Size;
        if (P < Base) {
            Base = P;
        }
        if (E > EndMax) {
            EndMax = E;
        }
    }
    if (Base == ~(UINT64)0) {
        Base = 0;
    }
    Base &= ~(UINT64)(PAGE_SIZE - 1);
    if (EndMax <= Base) {
        *OutBase = Base;
        *OutMax = 0;
        return;
    }
    Span = (UINT32)((EndMax - Base + PAGE_SIZE - 1) >> PAGE_SHIFT);
    if (Span > PHYSICAL_MEMORY_MAX_PAGES) {
        Span = PHYSICAL_MEMORY_MAX_PAGES;
    }
    *OutBase = Base;
    *OutMax = Span;
}

int PhysicalMemoryInit(void) {
    const BOOT_INFO *Info = BootInfoGet();
    UINT32 i;

    if (!Info || Info->RegionCount == 0) {
        return -1;
    }

    ComputeBaseAndMax(Info, &gPhysBase, &gMaxPage);
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

    DebugWrite("PMM: base=");
    DebugHex64(gPhysBase);
    DebugWrite(" free=");
    DebugHex64(gFreePages << PAGE_SHIFT);
    DebugWrite(" / tracked=");
    DebugHex64((UINT64)gMaxPage << PAGE_SHIFT);
    DebugWrite(" (");
    DebugHex32((UINT32)gFreePages);
    DebugWrite(" pages)\n");
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
                return (void *)(UINTN)PfnToPhys(Start);
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
    Pfn = PhysToPfn(Phys);
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
    Pfn = PhysToPfn(Phys);
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
    Pfn = PhysToPfn(Phys);
    for (i = 0; i < Count; i++) {
        if (Pfn + i >= gMaxPage) {
            return;
        }
        PhysicalMemoryReleasePage((void *)(UINTN)PfnToPhys(Pfn + i));
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
