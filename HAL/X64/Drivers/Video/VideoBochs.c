/*
 * VideoBochs.c — Bochs/QEMU VBE DISPI 热切（PR-H-video-split-1）
 *
 * 从 Video.c 迁出；只搬家、不改逻辑。
 */
#include "VideoPrivate.h"

static void BochsWrite(UINT16 Index, UINT16 Value) {
    HalIoWrite16(VBE_DISPI_IOPORT_INDEX, Index);
    HalIoWrite16(VBE_DISPI_IOPORT_DATA, Value);
}

static UINT16 BochsRead(UINT16 Index) {
    HalIoWrite16(VBE_DISPI_IOPORT_INDEX, Index);
    return HalIoRead16(VBE_DISPI_IOPORT_DATA);
}

static int BochsPresent(void) {
    UINT16 Id = BochsRead(VBE_DISPI_INDEX_ID);
    return (Id & 0xFFF0u) == VBE_DISPI_ID0;
}

int VideoBochsAvailable(void) {
    if (!gFront || gScreen.FrameBufferBase == 0) {
        return 0;
    }
    return BochsPresent();
}

/*
 * PR-G-hotres：经 Bochs DISPI 改 scanout 几何；LFB 基址沿用 Boot GOP。
 * 调用方须已映射足够大的帧缓冲，并在成功后重配后缓冲 / Gui。
 */
int VideoBochsSetMode(UINT32 Width, UINT32 Height) {
    UINT64 Need;
    UINT16 Mem64k;
    UINT64 Vram;
    VIDEO_CONFIG Cfg;

    if (Width < 320 || Height < 200 || Width > 4096 || Height > 4096) {
        return -1;
    }
    if (!gFront || gScreen.FrameBufferBase == 0) {
        return -1;
    }
    if (!BochsPresent()) {
        return -1;
    }

    Need = (UINT64)Width * (UINT64)Height * sizeof(UINT32);
    Mem64k = BochsRead(VBE_DISPI_INDEX_VIDEO_MEMORY_64K);
    if (Mem64k != 0) {
        Vram = (UINT64)Mem64k * 65536ull;
    } else if (gScreen.FrameBufferSize >= Need) {
        Vram = gScreen.FrameBufferSize;
    } else {
        Vram = 16ull * 1024 * 1024; /* QEMU VGA 常见默认 */
    }
    if (Vram < Need) {
        return -1;
    }

    BochsWrite(VBE_DISPI_INDEX_ENABLE, 0);
    BochsWrite(VBE_DISPI_INDEX_BANK, 0);
    BochsWrite(VBE_DISPI_INDEX_X_OFFSET, 0);
    BochsWrite(VBE_DISPI_INDEX_Y_OFFSET, 0);
    BochsWrite(VBE_DISPI_INDEX_BPP, 32);
    BochsWrite(VBE_DISPI_INDEX_XRES, (UINT16)Width);
    BochsWrite(VBE_DISPI_INDEX_YRES, (UINT16)Height);
    BochsWrite(VBE_DISPI_INDEX_VIRT_WIDTH, (UINT16)Width);
    BochsWrite(VBE_DISPI_INDEX_VIRT_HEIGHT, (UINT16)Height);
    BochsWrite(VBE_DISPI_INDEX_ENABLE, (UINT16)(VBE_DISPI_ENABLED | VBE_DISPI_LFB_ENABLED));

    /* 读回 DISPI，避免“软件记成 WxH、硬件仍是旧模式” */
    if (BochsRead(VBE_DISPI_INDEX_XRES) != (UINT16)Width ||
        BochsRead(VBE_DISPI_INDEX_YRES) != (UINT16)Height) {
        return -1;
    }

    VideoReleaseBackbuffer();
    Cfg.FrameBufferBase = gScreen.FrameBufferBase;
    Cfg.FrameBufferSize = Need;
    Cfg.HorizontalResolution = Width;
    Cfg.VerticalResolution = Height;
    Cfg.PixelsPerScanLine = Width;
    VideoSet(&Cfg);
    return 0;
}

