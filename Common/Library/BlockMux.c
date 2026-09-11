/*
 * BlockMux.c — Primary + MSC 双后端（PR-H-msc）
 *
 * DriveN 先试 Primary->Probe(N)；空位再挂 MSC（仅一次）。
 * PR-H-msc-1：MSC Probe 恒失败 → 行为与未安装 Mux 前的 Primary 一致。
 */
#include "BlockMux.h"
#include "Debug.h"

#define KIND_NONE 0
#define KIND_PRIM 1
#define KIND_MSC  2

static const BLOCK_BACKEND *gPrimary;
static const BLOCK_BACKEND *gMsc;
static UINT8 gKind[BLOCK_MAX_DRIVES];
static int gMscPlaced;

static int MuxProbe(UINT32 Drive);
static int MuxRead(UINT32 Drive, UINT32 Lba, UINT32 Count, void *Buffer);
static int MuxWrite(UINT32 Drive, UINT32 Lba, UINT32 Count, const void *Buffer);
static int MuxFlush(UINT32 Drive);

static const BLOCK_BACKEND gMuxBackend = {
    .Probe = MuxProbe,
    .ReadSectors = MuxRead,
    .WriteSectors = MuxWrite,
    .Flush = MuxFlush,
};

extern const BLOCK_BACKEND *BlockPeekBackend(void);

static int MuxProbe(UINT32 Drive) {
    if (Drive >= BLOCK_MAX_DRIVES) {
        return 0;
    }
    if (Drive == 0) {
        UINT32 i;
        gMscPlaced = 0;
        for (i = 0; i < BLOCK_MAX_DRIVES; i++) {
            gKind[i] = KIND_NONE;
        }
    }
    if (gPrimary && gPrimary->Probe(Drive)) {
        gKind[Drive] = KIND_PRIM;
        return 1;
    }
    if (gMsc && !gMscPlaced && gMsc->Probe(0)) {
        gKind[Drive] = KIND_MSC;
        gMscPlaced = 1;
        return 1;
    }
    return 0;
}

static int MuxRead(UINT32 Drive, UINT32 Lba, UINT32 Count, void *Buffer) {
    if (Drive >= BLOCK_MAX_DRIVES) {
        return 0;
    }
    if (gKind[Drive] == KIND_PRIM && gPrimary) {
        return gPrimary->ReadSectors(Drive, Lba, Count, Buffer);
    }
    if (gKind[Drive] == KIND_MSC && gMsc) {
        return gMsc->ReadSectors(0, Lba, Count, Buffer);
    }
    return 0;
}

static int MuxWrite(UINT32 Drive, UINT32 Lba, UINT32 Count, const void *Buffer) {
    if (Drive >= BLOCK_MAX_DRIVES) {
        return 0;
    }
    if (gKind[Drive] == KIND_PRIM && gPrimary && gPrimary->WriteSectors) {
        return gPrimary->WriteSectors(Drive, Lba, Count, Buffer);
    }
    if (gKind[Drive] == KIND_MSC && gMsc && gMsc->WriteSectors) {
        return gMsc->WriteSectors(0, Lba, Count, Buffer);
    }
    return 0;
}

static int MuxFlush(UINT32 Drive) {
    if (Drive >= BLOCK_MAX_DRIVES) {
        return 1;
    }
    if (gKind[Drive] == KIND_PRIM && gPrimary && gPrimary->Flush) {
        return gPrimary->Flush(Drive);
    }
    if (gKind[Drive] == KIND_MSC && gMsc && gMsc->Flush) {
        return gMsc->Flush(0);
    }
    return 1;
}

void BlockMuxInstallMsc(const BLOCK_BACKEND *Msc) {
    if (!Msc || !Msc->Probe || !Msc->ReadSectors || !Msc->WriteSectors) {
        DebugWrite("block-mux: bad msc\n");
        return;
    }
    if (!gPrimary) {
        gPrimary = BlockPeekBackend();
        if (gPrimary == &gMuxBackend) {
            gPrimary = 0;
        }
    }
    gMsc = Msc;
    BlockRegisterBackend(&gMuxBackend);
    DebugWrite("block-mux: msc installed\n");
}
