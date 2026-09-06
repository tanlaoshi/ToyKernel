/*
 * BlockAhci.c — AHCI 块设备经 Driver Block 类注册（PR-H1）
 *
 * 注册顺序在 Ata 之前；HalBlockInit 在 VMM 后再 Probe，AHCI 绑定时
 * ToyDriverBlockAttach 覆盖 ATA（真机无 IDE 时 ATA 本就不会绑）。
 */
#include "Block.h"
#include "Driver.h"
#include "DriverBlock.h"
#include "Ahci.h"
#include "VirtualMemory.h"
#include "Hal.h"

static const BLOCK_BACKEND gAhciBackend = {
    .Probe = AhciProbe,
    .ReadSectors = AhciReadSectors,
    .WriteSectors = AhciWriteSectors,
    .Flush = 0,
};

static int AhciDriverProbe(const TOY_DRIVER *Self, void *BusCtx, void **OutPriv) {
    (void)Self;
    (void)BusCtx;

    if (AhciReady()) {
        if (OutPriv) {
            *OutPriv = 0;
        }
        return 0;
    }
    /* HBA BAR 映射必须在 VMM Enable 之后（InitDriver 早 Probe 会跳过） */
    if (!VirtualMemoryEnabled()) {
        return -1;
    }
    if (!AhciSetup()) {
        return -1;
    }
    if (OutPriv) {
        *OutPriv = 0;
    }
    return 0;
}

static int AhciDriverBind(TOY_DRIVER_INSTANCE *Inst) {
    (void)Inst;
    return ToyDriverBlockAttach(&gAhciBackend);
}

static void AhciDriverRemove(TOY_DRIVER_INSTANCE *Inst) {
    (void)Inst;
}

static const TOY_DRIVER gAhciDriver = {
    .Name = "ahci",
    .Class = TOY_DRIVER_CLASS_BLOCK,
    .Match = 0,
    .Probe = AhciDriverProbe,
    .Bind = AhciDriverBind,
    .Remove = AhciDriverRemove,
};

void AhciDriverRegister(void) {
    (void)ToyDriverRegister(&gAhciDriver);
}
