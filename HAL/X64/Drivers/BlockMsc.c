/*
 * BlockMsc.c — USB MSC 经 Driver Block（PR-H-msc-1 空壳）
 *
 * Bind 只登记，不安装 Mux（避免改写 Primary 后端）。
 * Probe 看 UsbMscReady()，本刀恒为 0。
 */
#include "Block.h"
#include "Driver.h"
#include "DriverBlock.h"
#include "UsbMsc.h"
#include "Hal.h"
#include "VirtualMemory.h"
#include "Debug.h"

static int MscProbe(UINT32 Drive) {
    if (Drive != 0) {
        return 0;
    }
    return UsbMscReady() ? 1 : 0;
}

static int MscRead(UINT32 Drive, UINT32 Lba, UINT32 Count, void *Buffer) {
    (void)Drive;
    return UsbMscReadSectors(Lba, Count, Buffer);
}

static int MscWrite(UINT32 Drive, UINT32 Lba, UINT32 Count, const void *Buffer) {
    (void)Drive;
    return UsbMscWriteSectors(Lba, Count, Buffer);
}

static const BLOCK_BACKEND gMscBackend = {
    .Probe = MscProbe,
    .ReadSectors = MscRead,
    .WriteSectors = MscWrite,
    .Flush = 0,
};

static int MscDriverProbe(const TOY_DRIVER *Self, void *BusCtx, void **OutPriv) {
    (void)Self;
    (void)BusCtx;
    if (!VirtualMemoryEnabled()) {
        return -1;
    }
    if (OutPriv) {
        *OutPriv = 0;
    }
    return 0;
}

static int MscDriverBind(TOY_DRIVER_INSTANCE *Inst) {
    (void)Inst;
    /* PR-H-msc-1：不 BlockMuxInstallMsc，保持 AHCI/NVMe 后端不变 */
    DebugWrite("msc: PR-H-msc-1 scaffold (no mux)\n");
    (void)gMscBackend;
    return 0;
}

static void MscDriverRemove(TOY_DRIVER_INSTANCE *Inst) {
    (void)Inst;
}

static const TOY_DRIVER gMscDriver = {
    .Name = "usb-msc",
    .Class = TOY_DRIVER_CLASS_BLOCK,
    .Match = 0,
    .Probe = MscDriverProbe,
    .Bind = MscDriverBind,
    .Remove = MscDriverRemove,
};

void MscDriverRegister(void) {
    (void)ToyDriverRegister(&gMscDriver);
}
