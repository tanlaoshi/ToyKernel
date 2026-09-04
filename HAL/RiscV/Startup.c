/*
 * Startup.c — QEMU virt riscv64：OpenSBI + DTB → BOOT_INFO（PR-V1）/ ramfb（PR-V2）
 */
#include "HalSerial.h"
#include "Hal.h"
#include "BootInfo.h"
#include "Kernel.h"
#include "Dtb.h"
#include "Ramfb.h"

extern char __kernel_end[];

#define RISCV_VIRT_FWCFG_FALLBACK 0x10100000ULL

#if TOY_BRINGUP

void StartupMain(UINT64 HartId, UINT64 DtbPhys) {
    (void)HartId;
    (void)DtbPhys;
    HalSerialInit();
    HalSerialWrite("ToyOS RiscV virt: hello\n");
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

static void HexU64(UINT64 V) {
    static const char Hex[] = "0123456789abcdef";
    char Buf[17];
    int i;
    for (i = 15; i >= 0; i--) {
        Buf[i] = Hex[V & 0xf];
        V >>= 4;
    }
    Buf[16] = 0;
    HalSerialWrite(Buf);
}

void StartupMain(UINT64 HartId, UINT64 DtbPhys) {
    BOOT_INFO Info;
    /* OpenSBI 占 0x80000000；payload 链在 0x80200000（PR-V1） */
    UINT64 KernelStart = 0x80200000ULL;
    UINT64 KernelEnd = (UINT64)(UINTN)__kernel_end;
    UINT64 FirmwareEnd = KernelStart;
    UINT64 RamBase = 0x80000000ULL;
    UINT64 RamSize = 256ULL * 1024ULL * 1024ULL;
    UINT64 FreeStart;
    UINT64 FwCfg = 0;
    UINTN i;
    int FromDtb;

    (void)HartId;

    HalSerialInit();
    HalSerialWrite("ToyOS RiscV virt: KernelMain\n");

    FromDtb = (DtbMemoryRegion(DtbPhys, &RamBase, &RamSize) == 0);
    if (FromDtb) {
        HalSerialWrite("boot: DTB memory base=");
        HexU64(RamBase);
        HalSerialWrite(" size=");
        HexU64(RamSize);
        HalSerialWrite("\n");
    } else {
        HalSerialWrite("boot: DTB memory missing, fallback 256MiB @0x80000000\n");
        RamBase = 0x80000000ULL;
        RamSize = 256ULL * 1024ULL * 1024ULL;
    }

    for (i = 0; i < sizeof(Info); i++) {
        ((UINT8 *)&Info)[i] = 0;
    }
    Info.KernelStart = KernelStart;
    Info.KernelEnd = KernelEnd;

    FreeStart = (KernelEnd + 0xFFFULL) & ~0xFFFULL;
    if (FreeStart < FirmwareEnd) {
        FreeStart = FirmwareEnd;
    }

    if (FromDtb && DtbPhys != 0 && DtbFwCfgBase(DtbPhys, &FwCfg) != 0) {
        FwCfg = 0;
    }
    if (FwCfg == 0) {
        FwCfg = RISCV_VIRT_FWCFG_FALLBACK;
    }
    (void)RamfbSetup(&Info, FwCfg, &FreeStart, RamBase + RamSize);

    /* [RamBase, FreeStart) = OpenSBI + 内核 + FB（保留）；其后可分配 */
    if (FreeStart > RamBase && FreeStart - RamBase <= RamSize) {
        BootInfoAddRegion(&Info, RamBase, FreeStart - RamBase, 0);
        if (FreeStart < RamBase + RamSize) {
            BootInfoAddRegion(&Info, FreeStart, RamBase + RamSize - FreeStart, 1);
        }
    } else if (RamSize > 0) {
        BootInfoAddRegion(&Info, RamBase, RamSize, 1);
    }

    BootInfoSet(&Info);
    KernelMain();
    for (;;) {
        HalCpuPark();
    }
}

#endif
