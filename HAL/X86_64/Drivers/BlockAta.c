/*
 * BlockAta.c — x86 ATA PIO 块设备（PR-D2：经 Drv Block 类注册）
 */
#include "Block.h"
#include "Drv.h"
#include "DrvBlock.h"
#include "Ata.h"
#include "Hal.h"

static const BLOCK_BACKEND gAtaBackend = {
    .Probe = AtaProbe,
    .ReadSectors = AtaReadSectors,
    .WriteSectors = AtaWriteSectors,
    .Flush = 0,
};

static int AtaDrvProbe(const TOY_DRIVER *Self, void *BusCtx, void **OutPriv) {
    (void)Self;
    (void)BusCtx;
    /* 至少能 Probe 到 drive 0 才算有 ATA */
    if (!AtaProbe(0) && !AtaProbe(1)) {
        return -1;
    }
    if (OutPriv) {
        *OutPriv = 0;
    }
    return 0;
}

static int AtaDrvBind(TOY_DRV_INSTANCE *Inst) {
    (void)Inst;
    return ToyDrvBlockAttach(&gAtaBackend);
}

static void AtaDrvRemove(TOY_DRV_INSTANCE *Inst) {
    (void)Inst;
    /* Block 后端全局一份；Remove 不拆（重启才清） */
}

static const TOY_DRIVER gAtaDriver = {
    .Name = "ata-pio",
    .Class = TOY_DRV_CLASS_BLOCK,
    .Match = 0,
    .Probe = AtaDrvProbe,
    .Bind = AtaDrvBind,
    .Remove = AtaDrvRemove,
};

void HalDrvRegister(void) {
    (void)ToyDrvRegister(&gAtaDriver);
}

int HalBlockInit(void) {
    /* VMM 之后再 Probe Block 类（ATA 多在 InitDrv 已绑；此处补漏） */
    (void)ToyDrvProbeClass(TOY_DRV_CLASS_BLOCK);
    if (!BlockBackendReady()) {
        return 0;
    }
    return BlockInit();
}
