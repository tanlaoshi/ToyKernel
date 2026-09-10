/*
 * Platform.c — x86-64 固定 MMIO 映射与 Boot 传入的设备地址
 */
#include "Platform.h"
#include "VirtualMemory.h"

#define RUNTIME_RANGE_MAX 48
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
     * UEFI Runtime：GetTime 实现落在 Attribute&RUNTIME 的固件页。
     * 只映 ST/RT 表头而不映代码页 → 真机一调 GetTime 就缺页，屏停在 PHOTO。
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
