/*
 * IgpuMmio.c — PR-G-igpu-1：BAR0 UC 映入 + 只读指纹（不写 MMIO、不提交）
 *
 * 须在 VirtualMemory 启用后调用（Video 模块末）。失败软退，桌面仍走 GOP。
 */
#include "Igpu.h"
#include "PCIe.h"
#include "VirtualMemory.h"
#include "ToySerialLog.h"
#include "Hal.h"

#ifndef PTE_PWT
#define PTE_PWT HAL_PAGE_PWT
#define PTE_PCD HAL_PAGE_PCD
#endif

/* Gen9 BAR0 常 16MiB；指纹只读低部寄存器，先映 2MiB 够用 */
#define IGPU_MMIO_MAP_BYTES  (2u * 1024u * 1024u)
/* i915 GEN6_TIMESTAMP — Gen6+ 只读，未 forcewake 时也可能非 0xFFFFFFFF */
#define IGPU_REG_TIMESTAMP   0x2358u

static volatile UINT8 *gIgpuMmio;
static UINT64 gIgpuBarPhys;
static UINTN gIgpuMapBytes;
static int gIgpuMmioOk;

int IgpuMmioOk(void) {
    return gIgpuMmioOk;
}

UINT64 IgpuMmioBarPhys(void) {
    return gIgpuBarPhys;
}

volatile UINT8 *IgpuMmioBase(void) {
    return gIgpuMmio;
}

static UINT32 MmioR32(UINT32 Off) {
    volatile UINT32 *P;

    if (!gIgpuMmio || Off + 4u > gIgpuMapBytes) {
        return 0xFFFFFFFFu;
    }
    P = (volatile UINT32 *)(UINTN)(gIgpuMmio + Off);
    return *P;
}

static int ReadBar0(UINT8 Bus, UINT8 Dev, UINT8 Fn, UINT64 *BarOut) {
    UINT32 Lo;
    UINT32 Hi;
    UINT64 Bar;

    Lo = PciReadConfig(Bus, Dev, Fn, 0x10);
    if (Lo & 1u) {
        return 0; /* IO BAR：核显不应如此 */
    }
    Bar = Lo & 0xFFFFFFF0ULL;
    if (((Lo >> 1) & 3u) == 2u) {
        Hi = PciReadConfig(Bus, Dev, Fn, 0x14);
        Bar |= ((UINT64)Hi) << 32;
    }
    if (Bar == 0) {
        return 0;
    }
    *BarOut = Bar;
    return 1;
}

int IgpuMmioInit(void) {
    UINT8 Bus;
    UINT8 Dev;
    UINT8 Fn;
    UINT64 Bar;
    UINT32 Cmd;
    UINT64 Sz;
    UINTN MapBytes;
    UINT32 Ts;
    UINT32 Word0;

    if (gIgpuMmioOk) {
        return 1;
    }
    if (!IgpuProbed()) {
        return 0;
    }
    if (!VirtualMemoryEnabled()) {
        ToyLogBoot("Boot: igpu mmio defer (no VMM)\n");
        return 0;
    }

    Bus = IgpuPciBus();
    Dev = IgpuPciDev();
    Fn = IgpuPciFn();
    if (!ReadBar0(Bus, Dev, Fn, &Bar)) {
        ToyLogBoot("Boot: igpu mmio no BAR0\n");
        return 0;
    }

    /*
     * PciBarSize 会短暂把 BAR 写成全 1；MSE 开着会弄乱解码（见 IwlProbe）。
     * 先关 Memory Space → 量 size → 再开 MSE+BME。
     */
    Cmd = PciReadConfig(Bus, Dev, Fn, 0x04);
    PciWriteConfig(Bus, Dev, Fn, 0x04, Cmd & ~0x2u);
    Sz = PciBarSize(Bus, Dev, Fn, 0);
    PciWriteConfig(Bus, Dev, Fn, 0x04, Cmd | 0x06u);

    MapBytes = IGPU_MMIO_MAP_BYTES;
    if (Sz != 0 && Sz < MapBytes) {
        MapBytes = (UINTN)Sz;
    }
    if (MapBytes < 0x1000u) {
        ToyLogBoot("Boot: igpu mmio BAR too small\n");
        return 0;
    }

    /* 量 size 后重读 BAR（PciBarSize 应已恢复原值） */
    if (!ReadBar0(Bus, Dev, Fn, &Bar) || Bar == 0) {
        ToyLogBoot("Boot: igpu mmio BAR lost after size\n");
        return 0;
    }
    if (VirtualMemoryMapRange(Bar, Bar, MapBytes,
                              PTE_PRESENT | PTE_WRITABLE | PTE_PWT | PTE_PCD) != 0) {
        ToyLogBoot("Boot: igpu mmio map fail\n");
        return 0;
    }

    gIgpuBarPhys = Bar;
    gIgpuMmio = (volatile UINT8 *)(UINTN)Bar;
    gIgpuMapBytes = MapBytes;

    Word0 = MmioR32(0);
    Ts = MmioR32(IGPU_REG_TIMESTAMP);
    if (Word0 == 0xFFFFFFFFu && Ts == 0xFFFFFFFFu) {
        ToyLogBoot("Boot: igpu mmio read FFs — disable\n");
        gIgpuMmio = 0;
        gIgpuBarPhys = 0;
        gIgpuMapBytes = 0;
        return 0;
    }

    gIgpuMmioOk = 1;
    ToyLogBoot("Boot: igpu mmio bar=");
    ToyLogBootHex32((UINT32)Bar);
    ToyLogBoot(" sz=");
    ToyLogBootHex32((UINT32)MapBytes);
    ToyLogBoot(" ts=");
    ToyLogBootHex32(Ts);
    ToyLogBoot("\n");
    return 1;
}
