/*
 * Startup.c — QEMU virt aarch64：DTB → BOOT_INFO（PR-V1）/ ramfb（PR-V2）
 *
 * QEMU 对 ELF -kernel 不走 Linux 启动协议（x0 常为 0）。run-virt-arm.sh
 * 用 dumpdtb + -device loader 把 FDT 放到 ARM64_VIRT_DTB_ADDR。
 */
#include "HalSerial.h"
#include "Hal.h"
#include "BootInfo.h"
#include "Kernel.h"
#include "Dtb.h"
#include "Ramfb.h"

extern char __kernel_end[];

/* 与 run-virt-arm.sh 中 loader addr 一致（落在 ≥256MiB RAM 内） */
#define ARM64_VIRT_DTB_ADDR 0x4a000000ULL
#define ARM64_VIRT_FWCFG_FALLBACK 0x09020000ULL

#if TOY_BRINGUP

void StartupMain(UINT64 DtbPhys) {
    (void)DtbPhys;
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

static int TryDtb(UINT64 Phys, UINT64 *RamBase, UINT64 *RamSize) {
    if (Phys == 0) {
        return -1;
    }
    return DtbMemoryRegion(Phys, RamBase, RamSize);
}

void StartupMain(UINT64 DtbPhys) {
    BOOT_INFO Info;
    UINT64 KernelStart = 0x40000000ULL;
    UINT64 KernelEnd = (UINT64)(UINTN)__kernel_end;
    UINT64 RamBase = KernelStart;
    UINT64 RamSize = 256ULL * 1024ULL * 1024ULL;
    UINT64 FreeStart;
    UINT64 ReserveEnd;
    UINT64 UsedDtb = 0;
    UINT64 FwCfg = 0;
    UINTN i;
    int FromDtb;

    HalSerialInit();
    HalSerialWrite("ToyOS Arm64 virt: KernelMain\n");

    FromDtb = (TryDtb(DtbPhys, &RamBase, &RamSize) == 0);
    if (FromDtb) {
        UsedDtb = DtbPhys;
    } else if (TryDtb(ARM64_VIRT_DTB_ADDR, &RamBase, &RamSize) == 0) {
        FromDtb = 1;
        UsedDtb = ARM64_VIRT_DTB_ADDR;
    }

    if (FromDtb) {
        HalSerialWrite("boot: DTB @");
        HexU64(UsedDtb);
        HalSerialWrite(" memory base=");
        HexU64(RamBase);
        HalSerialWrite(" size=");
        HexU64(RamSize);
        HalSerialWrite("\n");
    } else {
        HalSerialWrite("boot: DTB memory missing, fallback 256MiB @0x40000000\n");
        RamBase = KernelStart;
        RamSize = 256ULL * 1024ULL * 1024ULL;
    }

    for (i = 0; i < sizeof(Info); i++) {
        ((UINT8 *)&Info)[i] = 0;
    }
    Info.KernelStart = KernelStart;
    Info.KernelEnd = KernelEnd;

    FreeStart = (KernelEnd + 0xFFFULL) & ~0xFFFULL;
    if (FreeStart < KernelStart) {
        FreeStart = KernelStart;
    }

    if (UsedDtb != 0 && DtbFwCfgBase(UsedDtb, &FwCfg) != 0) {
        FwCfg = 0;
    }
    if (FwCfg == 0) {
        FwCfg = ARM64_VIRT_FWCFG_FALLBACK;
    }
    (void)RamfbSetup(&Info, FwCfg, &FreeStart, RamBase + RamSize);

    ReserveEnd = FreeStart;
    if (ReserveEnd < RamBase) {
        ReserveEnd = RamBase;
    }
    if (ReserveEnd > RamBase && ReserveEnd - RamBase > 0) {
        UINT64 ResSize = ReserveEnd - RamBase;
        if (ResSize > RamSize) {
            ResSize = RamSize;
        }
        BootInfoAddRegion(&Info, RamBase, ResSize, 0);
    }
    if (FreeStart < RamBase + RamSize && FreeStart >= RamBase) {
        BootInfoAddRegion(&Info, FreeStart, RamBase + RamSize - FreeStart, 1);
    } else if (Info.RegionCount == 0 && RamSize > 0) {
        BootInfoAddRegion(&Info, RamBase, RamSize, 1);
    }

    BootInfoSet(&Info);
    KernelMain();
    for (;;) {
        HalCpuPark();
    }
}

#endif
