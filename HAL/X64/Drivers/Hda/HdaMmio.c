/*
 * HdaMmio.c — PR-G-audio-1：BAR0 UC 映入 + 只读控制器指纹（不写 MMIO）
 *
 * 须在 VirtualMemory 启用后调用（Video 模块末）。失败软退。
 */
#include "Hda.h"
#include "PCIe.h"
#include "VirtualMemory.h"
#include "ToySerialLog.h"
#include "Hal.h"

#ifndef PTE_PWT
#define PTE_PWT HAL_PAGE_PWT
#define PTE_PCD HAL_PAGE_PCD
#endif

/* HDA 规格：GCAP…OUTPAY 在控制器寄存器前 0x08；BAR 常见 16KiB */
#define HDA_REG_GCAP     0x00u
#define HDA_REG_VMIN     0x02u
#define HDA_REG_VMAJ     0x03u
#define HDA_REG_OUTPAY   0x04u
#define HDA_REG_INPAY    0x06u
#define HDA_MMIO_MIN     0x100u
#define HDA_MMIO_CAP     (16u * 1024u)

static volatile UINT8 *gHdaMmio;
static UINT64 gHdaBarPhys;
static UINTN gHdaMapBytes;
static int gHdaMmioOk;

int HdaMmioOk(void) {
    return gHdaMmioOk;
}

UINT64 HdaMmioBarPhys(void) {
    return gHdaBarPhys;
}

volatile UINT8 *HdaMmioBase(void) {
    return gHdaMmio;
}

UINTN HdaMmioMapBytes(void) {
    return gHdaMapBytes;
}

static UINT32 MmioR32(UINT32 Off) {
    volatile UINT32 *P;

    if (!gHdaMmio || Off + 4u > gHdaMapBytes) {
        return 0xFFFFFFFFu;
    }
    P = (volatile UINT32 *)(UINTN)(gHdaMmio + Off);
    return *P;
}

static UINT16 MmioR16(UINT32 Off) {
    volatile UINT16 *P;

    if (!gHdaMmio || Off + 2u > gHdaMapBytes) {
        return 0xFFFFu;
    }
    P = (volatile UINT16 *)(UINTN)(gHdaMmio + Off);
    return *P;
}

static UINT8 MmioR8(UINT32 Off) {
    if (!gHdaMmio || Off >= gHdaMapBytes) {
        return 0xFFu;
    }
    return gHdaMmio[Off];
}

UINT32 HdaMmioRead32(UINT32 Off) {
    return MmioR32(Off);
}

UINT16 HdaMmioRead16(UINT32 Off) {
    return MmioR16(Off);
}

UINT8 HdaMmioRead8(UINT32 Off) {
    return MmioR8(Off);
}

static int ReadBar0(UINT8 Bus, UINT8 Dev, UINT8 Fn, UINT64 *BarOut) {
    UINT32 Lo;
    UINT32 Hi;
    UINT64 Bar;

    Lo = PciReadConfig(Bus, Dev, Fn, 0x10);
    if (Lo & 1u) {
        return 0; /* IO BAR：HDA 应为 MMIO */
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

int HdaMmioInit(void) {
    UINT8 Bus;
    UINT8 Dev;
    UINT8 Fn;
    UINT64 Bar;
    UINT32 Cmd;
    UINT64 Sz;
    UINTN MapBytes;
    UINT16 Gcap;
    UINT8 Vmin;
    UINT8 Vmaj;
    UINT16 Outpay;
    UINT16 Inpay;

    if (gHdaMmioOk) {
        return 1;
    }
    if (!HdaProbed()) {
        return 0;
    }
    if (!VirtualMemoryEnabled()) {
        ToyLogBoot("Boot: hda mmio defer (no VMM)\n");
        return 0;
    }

    Bus = HdaPciBus();
    Dev = HdaPciDev();
    Fn = HdaPciFn();
    if (!ReadBar0(Bus, Dev, Fn, &Bar)) {
        ToyLogBoot("Boot: hda mmio no BAR0\n");
        return 0;
    }

    /*
     * PciBarSize 会短暂把 BAR 写成全 1；MSE 开着会弄乱解码（见 IgpuMmio/Iwl）。
     * 先关 Memory Space → 量 size → 再开 MSE+BME。
     */
    Cmd = PciReadConfig(Bus, Dev, Fn, 0x04);
    PciWriteConfig(Bus, Dev, Fn, 0x04, Cmd & ~0x2u);
    Sz = PciBarSize(Bus, Dev, Fn, 0);
    PciWriteConfig(Bus, Dev, Fn, 0x04, Cmd | 0x06u);

    MapBytes = HDA_MMIO_CAP;
    if (Sz != 0 && Sz < MapBytes) {
        MapBytes = (UINTN)Sz;
    }
    if (MapBytes < HDA_MMIO_MIN) {
        ToyLogBoot("Boot: hda mmio BAR too small\n");
        return 0;
    }

    if (!ReadBar0(Bus, Dev, Fn, &Bar) || Bar == 0) {
        ToyLogBoot("Boot: hda mmio BAR lost after size\n");
        return 0;
    }
    if (VirtualMemoryMapRange(Bar, Bar, MapBytes,
                              PTE_PRESENT | PTE_WRITABLE | PTE_PWT | PTE_PCD) != 0) {
        ToyLogBoot("Boot: hda mmio map fail\n");
        return 0;
    }

    gHdaBarPhys = Bar;
    gHdaMmio = (volatile UINT8 *)(UINTN)Bar;
    gHdaMapBytes = MapBytes;

    Gcap = MmioR16(HDA_REG_GCAP);
    Vmin = MmioR8(HDA_REG_VMIN);
    Vmaj = MmioR8(HDA_REG_VMAJ);
    Outpay = MmioR16(HDA_REG_OUTPAY);
    Inpay = MmioR16(HDA_REG_INPAY);

    if (Gcap == 0xFFFFu && Vmaj == 0xFFu && Vmin == 0xFFu) {
        ToyLogBoot("Boot: hda mmio read FFs — disable\n");
        gHdaMmio = 0;
        gHdaBarPhys = 0;
        gHdaMapBytes = 0;
        return 0;
    }

    gHdaMmioOk = 1;
    ToyLogBoot("Boot: hda mmio bar=");
    ToyLogBootHex32((UINT32)Bar);
    ToyLogBoot(" sz=");
    ToyLogBootHex32((UINT32)MapBytes);
    ToyLogBoot(" gcap=");
    ToyLogBootHex32((UINT32)Gcap);
    ToyLogBoot(" v=");
    ToyLogBootHex32((UINT32)Vmaj);
    ToyLogBoot(".");
    ToyLogBootHex32((UINT32)Vmin);
    ToyLogBoot(" outpay=");
    ToyLogBootHex32((UINT32)Outpay);
    ToyLogBoot(" inpay=");
    ToyLogBootHex32((UINT32)Inpay);
    ToyLogBoot("\n");
    return 1;
}
