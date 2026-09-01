/*
 * BlockAta.c — x86 ATA PIO 块设备后端
 */
#include "Block.h"
#include "Ata.h"
#include "Hal.h"

static const BLOCK_BACKEND gAtaBackend = {
    AtaProbe,
    AtaReadSectors,
    AtaWriteSectors,
};

int HalBlockInit(void) {
    BlockRegisterBackend(&gAtaBackend);
    return BlockInit();
}
