/*
 * BlockNvme.c — NVMe 块设备经 Driver Block 类注册（PR-H5）
 *
 * 注册在 AHCI 之后；HalBlockInit 再 Probe 时若 NVMe 在场则覆盖 AHCI/ATA。
 */
#include "Block.h"
#include "Driver.h"
#include "DriverBlock.h"
#include "Nvme.h"
#include "VirtualMemory.h"
#include "Hal.h"

static const BLOCK_BACKEND gNvmeBackend = {
    .Probe = NvmeProbe,
    .ReadSectors = NvmeReadSectors,
    .WriteSectors = NvmeWriteSectors,
    .Flush = 0,
};

static int NvmeDriverProbe(const TOY_DRIVER *Self, void *BusCtx, void **OutPriv) {
    (void)Self;
    (void)BusCtx;

    if (NvmeReady()) {
        if (OutPriv) {
            *OutPriv = 0;
        }
        return 0;
    }
    if (!VirtualMemoryEnabled()) {
        return -1;
    }
    if (!NvmeSetup()) {
        return -1;
    }
    if (OutPriv) {
        *OutPriv = 0;
    }
    return 0;
}

static int NvmeDriverBind(TOY_DRIVER_INSTANCE *Inst) {
    (void)Inst;
    return ToyDriverBlockAttach(&gNvmeBackend);
}

static void NvmeDriverRemove(TOY_DRIVER_INSTANCE *Inst) {
    (void)Inst;
}

static const TOY_DRIVER gNvmeDriver = {
    .Name = "nvme",
    .Class = TOY_DRIVER_CLASS_BLOCK,
    .Match = 0,
    .Probe = NvmeDriverProbe,
    .Bind = NvmeDriverBind,
    .Remove = NvmeDriverRemove,
};

void NvmeDriverRegister(void) {
    (void)ToyDriverRegister(&gNvmeDriver);
}
