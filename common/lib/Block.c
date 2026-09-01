/*
 * Block.c — 块设备读抽象，后端为 ATA Primary Master/Slave
 */
#include "Block.h"
#include "Ata.h"
#include "Debug.h"

static UINT32 gDrive;
static UINT8  gReady[BLOCK_MAX_DRIVES];

int BlockSelect(UINT32 Drive) {
    if (Drive >= BLOCK_MAX_DRIVES || !gReady[Drive]) {
        return 0;
    }
    gDrive = Drive;
    return 1;
}

UINT32 BlockCurrentDrive(void) {
    return gDrive;
}

int BlockInit(void) {
    UINT32 n = 0;
    UINT32 d;

    for (d = 0; d < BLOCK_MAX_DRIVES; d++) {
        gReady[d] = 0;
        if (AtaProbe(d)) {
            gReady[d] = 1;
            n++;
            DebugWrite("block: drive ");
            DebugHex32(d);
            DebugWrite(" ready\n");
        }
    }
    gDrive = 0;
    if (n == 0) {
        DebugWrite("block: no drives\n");
        return 0;
    }
    if (!gReady[0]) {
        for (d = 0; d < BLOCK_MAX_DRIVES; d++) {
            if (gReady[d]) {
                gDrive = d;
                break;
            }
        }
    }
    return (int)n;
}

int BlockReadSectors(UINT32 Lba, UINT32 Count, void *Buffer) {
    return AtaReadSectors(gDrive, Lba, Count, Buffer);
}

int BlockWriteSectors(UINT32 Lba, UINT32 Count, const void *Buffer) {
    return AtaWriteSectors(gDrive, Lba, Count, Buffer);
}
