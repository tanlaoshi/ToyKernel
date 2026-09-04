/*
 * Startup.c — QEMU virt aarch64：BRINGUP hello 或完整 KernelMain（PR-A7）
 */
#include "HalSerial.h"
#include "Hal.h"
#include "BootInfo.h"
#include "Kernel.h"

extern char __kernel_end[];

#if TOY_BRINGUP

void StartupMain(void) {
    HalSerialInit();
    HalSerialWrite("ToyOS Arm64 virt: hello\n");
    HalCpuHalt();
}

#else

static int BootInfoAddRegion(BOOT_INFO *Info, UINT64 Phys, UINT64 Size, int Free) {
    if (Info->RegionCount >= BOOT_MEMORY_REGIONS_MAX || Size == 0) {
        return -1;
    }
    Info->Regions[Info->RegionCount].Phys = Phys;
    Info->Regions[Info->RegionCount].Size = Size;
    Info->Regions[Info->RegionCount].Free = Free ? 1u : 0u;
    Info->RegionCount++;
    return 0;
}

void StartupMain(void) {
    BOOT_INFO Info;
    UINT64 KernelStart = 0x40000000ULL;
    UINT64 KernelEnd = (UINT64)(UINTN)__kernel_end;
    UINT64 RamSize = 256ULL * 1024ULL * 1024ULL;
    UINT64 FreeStart;
    UINTN i;

    HalSerialInit();
    HalSerialWrite("ToyOS Arm64 virt: KernelMain\n");

    for (i = 0; i < sizeof(Info); i++) {
        ((UINT8 *)&Info)[i] = 0;
    }
    Info.KernelStart = KernelStart;
    Info.KernelEnd = KernelEnd;

    FreeStart = (KernelEnd + 0xFFFULL) & ~0xFFFULL;
    BootInfoAddRegion(&Info, KernelStart, FreeStart - KernelStart, 0);
    if (FreeStart < KernelStart + RamSize) {
        BootInfoAddRegion(&Info, FreeStart, KernelStart + RamSize - FreeStart, 1);
    }
    BootInfoSet(&Info);
    KernelMain();
    for (;;) {
        HalCpuPark();
    }
}

#endif
