/*
 * BlockMsc.c — USB MSC 经 Driver Block（PR-H-msc）
 *
 * Bind 只登记，不自动 Install Mux（默认启动不认盘）。
 * Shell `msc mount` → BlockMscBackend + BlockMuxInstallMsc + remount。
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

const BLOCK_BACKEND *BlockMscBackend(void) {
    return &gMscBackend;
}

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
    /* PR-H-msc-6：仍不自动 Mux；显式 msc mount */
    DebugWrite("msc: bound (mux on msc mount)\n");
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
