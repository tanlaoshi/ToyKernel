/*
 * BlockAhci.c — AHCI 块设备经 Drv Block 类注册（PR-H1）
 *
 * 注册顺序在 Ata 之前；HalBlockInit 在 VMM 后再 Probe，AHCI 绑定时
 * ToyDrvBlockAttach 覆盖 ATA（真机无 IDE 时 ATA 本就不会绑）。
 */
#include "Block.h"
#include "Drv.h"
#include "DrvBlock.h"
#include "Ahci.h"
#include "VirtualMemory.h"
#include "Hal.h"

static const BLOCK_BACKEND gAhciBackend = {
    .Probe = AhciProbe,
    .ReadSectors = AhciReadSectors,
    .WriteSectors = AhciWriteSectors,
    .Flush = 0,
};

static int AhciDrvProbe(const TOY_DRIVER *Self, void *BusCtx, void **OutPriv) {
    (void)Self;
    (void)BusCtx;

    if (AhciReady()) {
        if (OutPriv) {
            *OutPriv = 0;
        }
        return 0;
    }
    /* HBA BAR 映射必须在 VMM Enable 之后（InitDrv 早 Probe 会跳过） */
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

static int AhciDrvBind(TOY_DRV_INSTANCE *Inst) {
    (void)Inst;
    return ToyDrvBlockAttach(&gAhciBackend);
}

static void AhciDrvRemove(TOY_DRV_INSTANCE *Inst) {
    (void)Inst;
}

static const TOY_DRIVER gAhciDriver = {
    .Name = "ahci",
    .Class = TOY_DRV_CLASS_BLOCK,
    .Match = 0,
    .Probe = AhciDrvProbe,
    .Bind = AhciDrvBind,
    .Remove = AhciDrvRemove,
};

void AhciDrvRegister(void) {
    (void)ToyDrvRegister(&gAhciDriver);
}
