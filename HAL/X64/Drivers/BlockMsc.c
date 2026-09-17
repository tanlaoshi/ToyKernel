/*
 * BlockMsc.c — USB MSC 经 Driver Block（PR-H-msc）
 *
 * Bind 只登记，不装 Mux（启动不自动认盘）。
 * PR-H-msc-6：`msc mount` → BlockMscInstall → BlockMuxInstallMsc。
 */
#include "Block.h"
#include "BlockMux.h"
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

static int MscFlush(UINT32 Drive) {
    (void)Drive;
    return UsbMscFlush();
}

static const BLOCK_BACKEND gMscBackend = {
    .Probe = MscProbe,
    .ReadSectors = MscRead,
    .WriteSectors = MscWrite,
    .Flush = MscFlush,
};

void BlockMscInstall(void) {
    BlockMuxInstallMsc(&gMscBackend);
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
    /* 启动不装 Mux；显式 `msc mount` 才 BlockMscInstall */
    DebugWrite("msc: registered (mount via shell)\n");
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
