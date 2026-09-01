/*
 * Platform.c — x86-64 固定 MMIO 映射与 Boot 传入的设备地址
 */
#include "Platform.h"
#include "VirtualMemory.h"

static UINT64 gXhciFallback;

void HalPlatformSetXhciFallback(UINT64 Address) {
    gXhciFallback = Address;
}

UINT64 HalPlatformXhciFallback(void) {
    return gXhciFallback;
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
    MapIdentityRange(0xFEE00000ULL, 0x100000ULL);
    if (gXhciFallback != 0) {
        MapIdentityRange(gXhciFallback, 0x1000000ULL);
    }
}
