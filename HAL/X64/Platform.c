/*
 * Platform.c — x86-64 固定 MMIO 映射与 Boot 传入的设备地址
 */
#include "Platform.h"
#include "VirtualMemory.h"

#define RUNTIME_RANGE_MAX 96 /* Runtime + 高址 Boot/Loader（GOP SetMode） */
#define EFI_MEMORY_RUNTIME (1ULL << 63)

static UINT64 gXhciFallback;
static UINT64 gRsdp;
static void *gSystemTable;

static struct {
    UINT64 Phys;
    UINT64 Size;
} gRuntimeRanges[RUNTIME_RANGE_MAX];
static int gRuntimeRangeCount;

void HalPlatformSetXhciFallback(UINT64 Address) {
    gXhciFallback = Address;
}

UINT64 HalPlatformXhciFallback(void) {
    return gXhciFallback;
}

void HalPlatformSetRsdp(UINT64 Address) {
    gRsdp = Address;
}

UINT64 HalPlatformRsdp(void) {
    return gRsdp;
}

void HalPlatformSetSystemTable(void *SystemTable) {
    gSystemTable = SystemTable;
}

void *HalPlatformSystemTable(void) {
    return gSystemTable;
}

void HalPlatformNoteRuntimeRange(UINT64 Phys, UINT64 Size) {
    if (Size == 0 || gRuntimeRangeCount >= RUNTIME_RANGE_MAX) {
        return;
    }
    gRuntimeRanges[gRuntimeRangeCount].Phys = Phys;
    gRuntimeRanges[gRuntimeRangeCount].Size = Size;
    gRuntimeRangeCount++;
}

int HalPlatformRuntimeRangeCount(void) {
    return gRuntimeRangeCount;
}

static void MapIdentityRange(UINT64 Phys, UINT64 Size) {
    if (Size == 0) {
        return;
    }
    UINT64 Start = Phys & ~(UINT64)(4096 - 1);
    UINT64 End = Phys + Size;
    while (Start < End) {
        VirtualMemoryMapPage(Start, Start, PTE_PRESENT | PTE_WRITABLE);
        Start += 4096;
    }
}

void HalPlatformMapMmio(void) {
    int i;

    MapIdentityRange(0xFEE00000ULL, 0x100000ULL); /* LAPIC */
    MapIdentityRange(0xFEC00000ULL, 0x1000ULL);   /* IOAPIC 默认；MADT 另址时 IoApicInit 再映 */
    if (gXhciFallback != 0) {
        MapIdentityRange(gXhciFallback, 0x1000000ULL);
    }
    /*
     * UEFI Runtime + 高址 Boot/Loader（Startup 记入）：GetTime / GOP SetMode
     * 实现落在这些页。只映表头不映代码/数据 → 真机缺页。
     */
    for (i = 0; i < gRuntimeRangeCount; i++) {
        MapIdentityRange(gRuntimeRanges[i].Phys, gRuntimeRanges[i].Size);
    }
    if (gSystemTable) {
        UINT64 StPhys = (UINT64)(UINTN)gSystemTable;
        MapIdentityRange(StPhys, 0x1000);
        {
            void *Rt = *(void **)(UINTN)(StPhys + 88);
            if (Rt) {
                MapIdentityRange((UINT64)(UINTN)Rt, 0x1000);
            }
        }
    }
}
