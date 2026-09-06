/*
 * Startup.c — RISC-V：DTB / 板级表 → BOOT_INFO（PR-V1）/ ramfb（virt）/ Duo S（PR-B3）
 *
 * OpenSBI / 厂商 U-Boot：a0=hartid，a1=DTB。
 */
#include "HalSerial.h"
#include "Hal.h"
#include "BootInfo.h"
#include "Kernel.h"
#include "Dtb.h"
#include "Ramfb.h"
#include "Board.h"

extern char __kernel_end[];

#define RISCV_VIRT_FWCFG_FALLBACK 0x10100000ULL

#if TOY_BRINGUP

void StartupMain(UINT64 HartId, UINT64 DtbPhys) {
    (void)HartId;
    (void)DtbPhys;
    HalSerialInit();
    HalSerialWrite("ToyOS RiscV: hello\n");
    HalSerialWrite("board: ");
    HalSerialWrite(BoardName());
    HalSerialWrite("\n");
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
    UINT64 KernelStart = TOY_BOARD_KERNEL_LOAD;
    UINT64 KernelEnd = (UINT64)(UINTN)__kernel_end;
    UINT64 FirmwareEnd = KernelStart;
    UINT64 RamBase = TOY_BOARD_RAM_BASE;
    UINT64 RamSize = TOY_BOARD_RAM_SIZE;
    UINT64 FreeStart;
    UINT64 FwCfg = 0;
    UINTN i;
    int FromDtb;

    (void)HartId;

    HalSerialInit();
    HalSerialWrite("ToyOS RiscV: KernelMain\n");
    HalSerialWrite("board: ");
    HalSerialWrite(BoardName());
    HalSerialWrite("\n");

    FromDtb = (DtbMemoryRegion(DtbPhys, &RamBase, &RamSize) == 0);
    if (FromDtb) {
        HalSerialWrite("boot: DTB memory base=");
        HexU64(RamBase);
        HalSerialWrite(" size=");
        HexU64(RamSize);
        HalSerialWrite("\n");
    } else {
        HalSerialWrite("boot: DTB memory missing, fallback BoardConfig RAM\n");
        RamBase = TOY_BOARD_RAM_BASE;
        RamSize = TOY_BOARD_RAM_SIZE;
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

#if TOY_BOARD_HAS_FRAMEBUFFER
    if (FromDtb && DtbPhys != 0 && DtbFwCfgBase(DtbPhys, &FwCfg) != 0) {
        FwCfg = 0;
    }
    if (FwCfg == 0) {
        FwCfg = RISCV_VIRT_FWCFG_FALLBACK;
    }
    (void)RamfbSetup(&Info, FwCfg, &FreeStart, RamBase + RamSize);
#else
    (void)FwCfg;
#endif

    /* [RamBase, FreeStart) = 固件 + 内核（+ 可选 FB）；其后可分配 */
    if (FreeStart > RamBase && FreeStart - RamBase <= RamSize) {
        BootInfoAddRegion(&Info, RamBase, FreeStart - RamBase, 0);
        if (FreeStart < RamBase + RamSize) {
            BootInfoAddRegion(&Info, FreeStart, RamBase + RamSize - FreeStart, 1);
        }
    } else if (RamSize > 0) {
        BootInfoAddRegion(&Info, RamBase, RamSize, 1);
    }

    BootInfoSet(&Info);
    HalSmpNoteDtb(DtbPhys);
    KernelMain();
    for (;;) {
        HalCpuPark();
    }
}

#endif
