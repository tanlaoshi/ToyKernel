/*
 * Igpu.c — PR-G-igpu-0：Intel VGA/Display PCI 认卡（NUC7 UHD 620）
 *
 * 只认卡 + lsdev；不映 BAR、不提交命令。无卡（QEMU）软退不挡桌面。
 */
#include "Igpu.h"
#include "Driver.h"
#include "Device.h"
#include "ToySerialLog.h"

static int gIgpuProbed;
static UINT16 gIgpuDid;
static UINT8 gIgpuBus;
static UINT8 gIgpuDev;
static UINT8 gIgpuFn;
static DEVICE_NODE *gIgpuNode;

int IgpuProbed(void) {
    return gIgpuProbed;
}

UINT16 IgpuPciDid(void) {
    return gIgpuDid;
}

UINT8 IgpuPciBus(void) {
    return gIgpuBus;
}

UINT8 IgpuPciDev(void) {
    return gIgpuDev;
}

UINT8 IgpuPciFn(void) {
    return gIgpuFn;
}

/*
 * 自扫设备表：Vendor 8086 + Class 03（Display）。
 * 不依赖 DID 白名单——NUC7 8650U 常见 0x5917，其它 Gen9 也认，串口打出实 DID。
 */
static DEVICE_NODE *FindIntelDisplay(void) {
    int Idx;

    for (Idx = 0; Idx < DeviceCount(); Idx++) {
        DEVICE_NODE *Dev = DeviceGet(Idx);

        if (!Dev || Dev->Bound || Dev->Bus != DEVICE_BUS_PCI) {
            continue;
        }
        if (Dev->Vendor != 0x8086u) {
            continue;
        }
        if (Dev->Class != 0x03u) {
            continue;
        }
        /* 0x00 VGA；0x80 Other display controller（部分固件） */
        if (Dev->Subclass != 0x00u && Dev->Subclass != 0x80u) {
            continue;
        }
        return Dev;
    }
    return 0;
}

static int IgpuProbe(const TOY_DRIVER *Self, void *BusCtx, void **OutPrivate) {
    DEVICE_NODE *Dev;

    (void)Self;
    (void)BusCtx;
    if (OutPrivate) {
        *OutPrivate = 0;
    }

    Dev = FindIntelDisplay();
    if (!Dev) {
        ToyLogBoot("Boot: igpu skip (no Intel display)\n");
        return -1;
    }

    gIgpuNode = Dev;
    gIgpuDid = Dev->Device;
    gIgpuBus = Dev->PciBus;
    gIgpuDev = Dev->PciDev;
    gIgpuFn = Dev->PciFn;
    gIgpuProbed = 1;

    ToyLogBoot("Boot: igpu probe did=");
    ToyLogBootHex32((UINT32)gIgpuDid);
    ToyLogBoot(" @");
    ToyLogBootHex32((UINT32)gIgpuBus);
    ToyLogBoot(":");
    ToyLogBootHex32((UINT32)gIgpuDev);
    ToyLogBoot(".");
    ToyLogBootHex32((UINT32)gIgpuFn);
    ToyLogBoot("\n");
    return 0;
}

static int IgpuBind(TOY_DRIVER_INSTANCE *Inst) {
    if (!Inst || !Inst->Driver || !gIgpuNode) {
        return -1;
    }
    DeviceBindDriver(gIgpuNode, Inst->Driver, Inst);
    ToyLogBoot("Boot: igpu bound (probe-only)\n");
    return 0;
}

static void IgpuRemove(TOY_DRIVER_INSTANCE *Inst) {
    (void)Inst;
    gIgpuProbed = 0;
    gIgpuNode = 0;
    gIgpuDid = 0;
}

static const TOY_DRIVER gIgpuDriver = {
    .Name = "igpu",
    .Class = TOY_DRIVER_CLASS_DISPLAY,
    .Match = 0, /* 自扫 Class 03；见 FindIntelDisplay */
    .Probe = IgpuProbe,
    .Bind = IgpuBind,
    .Remove = IgpuRemove,
};

void IgpuDriverRegister(void) {
    if (ToyDriverRegister(&gIgpuDriver) != 0) {
        ToyLogBoot("Boot: igpu register fail\n");
        return;
    }
    (void)ToyDriverProbeClass(TOY_DRIVER_CLASS_DISPLAY);
}
