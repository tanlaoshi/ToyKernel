/*
 * PhysicalMemory.c — 物理页分配器（位图实现）
 */
#include "PhysicalMemory.h"
#include "Console.h"
#include "Debug.h"

#define EFI_MEMORY_CONVENTIONAL 7

#define PHYSICAL_MEMORY_MAX_PAGES (128u * 1024u)

typedef struct {
    UINT32 Type;
    UINT32 Pad;
    UINT64 PhysicalStart;
    UINT64 VirtualStart;
    UINT64 NumberOfPages;
    UINT64 Attribute;
} EFI_MEMORY_DESCRIPTOR;

extern char __kernel_end[];

static UINT8  gBitmap[PHYSICAL_MEMORY_MAX_PAGES / 8];
static UINT32 gMaxPage;
static UINT64 gFreePages;

/* 查询页帧号 Pfn 是否已占用 */
static int PageUsed(UINT32 Pfn) {
    if (Pfn >= gMaxPage) {
        return 1;
    }
    return (gBitmap[Pfn / 8] >> (Pfn % 8)) & 1;
}

/* 设置页占用状态并维护 gFreePages 计数 */
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

/* 将 [Phys, Phys+Size) 范围内所有页标记为已用 */
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

/* 将 Conventional 内存区域标为空闲 */
static void AddConventional(UINT64 Phys, UINT64 Pages) {
    for (UINT64 i = 0; i < Pages; i++) {
        UINT64 Addr = Phys + (i << PAGE_SHIFT);
        UINT32 Pfn = (UINT32)(Addr >> PAGE_SHIFT);
        if (Pfn < gMaxPage) {
            SetPage(Pfn, 0);
        }
    }
}

/* 根据内存映射计算需要跟踪的最大页帧号 */
static UINT32 ComputeMaxPage(MEMORY_MAP *Map) {
    UINT32 Max = 0;
    UINT8 *Base = (UINT8 *)Map->Buffer;
    UINTN Count = Map->MapSize / Map->DescriptorSize;

    for (UINTN i = 0; i < Count; i++) {
        EFI_MEMORY_DESCRIPTOR *Desc =
            (EFI_MEMORY_DESCRIPTOR *)(Base + i * Map->DescriptorSize);
        UINT64 End = Desc->PhysicalStart + (Desc->NumberOfPages << PAGE_SHIFT);
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

/* 初始化 PMM：解析映射、标记保留区、打印空闲统计 */
int PhysicalMemoryInit(const PHYSICAL_MEMORY_BOOT_INFO *Info) {
    MEMORY_MAP *Map = Info->Map;
    if (Map == 0 || Map->Buffer == 0 || Map->DescriptorSize < sizeof(EFI_MEMORY_DESCRIPTOR)) {
        return -1;
    }

    gMaxPage = ComputeMaxPage(Map);
    if (gMaxPage == 0) {
        return -1;
    }

    for (UINT32 i = 0; i < (PHYSICAL_MEMORY_MAX_PAGES / 8); i++) {
        gBitmap[i] = 0xFF;
    }
    gFreePages = 0;

    UINT8 *Base = (UINT8 *)Map->Buffer;
    UINTN Count = Map->MapSize / Map->DescriptorSize;
    for (UINTN i = 0; i < Count; i++) {
        EFI_MEMORY_DESCRIPTOR *Desc =
            (EFI_MEMORY_DESCRIPTOR *)(Base + i * Map->DescriptorSize);
        if (Desc->Type != EFI_MEMORY_CONVENTIONAL) {
            continue;
        }
        AddConventional(Desc->PhysicalStart, Desc->NumberOfPages);
    }

    ReserveRange(0, PAGE_SIZE);
    ReserveRange(Info->KernelStart, Info->KernelEnd - Info->KernelStart);
    ReserveRange(Info->BootConfigPhys, PAGE_SIZE);
    ReserveRange((UINT64)(UINTN)Map->Buffer, Map->MapSize);
    if (Info->FrameBufferSize != 0) {
        ReserveRange(Info->FrameBufferBase, Info->FrameBufferSize);
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

/* 分配 Count 个连续物理页，返回恒等映射虚拟地址，失败返回 NULL */
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
                }
                return (void *)(UINTN)(Start << PAGE_SHIFT);
            }
        } else {
            Run = 0;
        }
    }
    return 0;
}

/* 分配单个 4KB 页 */
void *PhysicalMemoryAllocatePage(void) {
    return PhysicalMemoryAllocatePages(1);
}

/* 释放从 Page 起的 Count 个连续页 */
void PhysicalMemoryFreePages(void *Page, UINT32 Count) {
    UINT64 Phys = (UINT64)(UINTN)Page;
    if ((Phys & (PAGE_SIZE - 1)) != 0) {
        return;
    }
    UINT32 Pfn = (UINT32)(Phys >> PAGE_SHIFT);
    for (UINT32 i = 0; i < Count; i++) {
        if (Pfn + i >= gMaxPage) {
            return;
        }
        SetPage(Pfn + i, 0);
    }
}

/* 释放单页 */
void PhysicalMemoryFreePage(void *Page) {
    PhysicalMemoryFreePages(Page, 1);
}

/* 返回跟踪范围内的总页数 */
UINT64 PhysicalMemoryTotalPages(void) {
    return gMaxPage;
}

/* 返回当前空闲页数 */
UINT64 PhysicalMemoryFreePageCount(void) {
    return gFreePages;
}
