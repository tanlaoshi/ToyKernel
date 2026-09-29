/*
 * HalVideo.c — PR-S3-halvideo-1：模式 / Present / 后缓冲 / 缩放
 *
 * FB WC 见 HalVideoFb.c；像素/文字绘制见 HalVideoDraw.c。
 */
#include "HalVideo.h"
#include "Video.h"
#include "PhysicalMemory.h"
#include "HalDevices.h"
#include "BootInfo.h"

void HalVideoSet(const VIDEO_CONFIG *Config) {
    VIDEO_CONFIG Local;

    if (!Config) {
        Local.FrameBufferBase = 0;
        Local.FrameBufferSize = 0;
        Local.HorizontalResolution = 0;
        Local.VerticalResolution = 0;
        Local.PixelsPerScanLine = 0;
    } else {
        Local = *Config;
    }
    VideoSet(&Local);
}

/*
 * PR-G9：PMM 分配与屏同尺寸后缓冲并挂上。须在 PhysicalMemoryInitialize 之后调用。
 * 分配失败则保持直写 GOP（功能仍可用，仍可能撕裂）。
 */
void HalVideoInitBackbuffer(void) {
    UINT32 W;
    UINT32 H;
    UINT64 Bytes;
    UINT32 Pages;
    UINT32 *Buf;

    HalIgpuPresentInvalidate();
    VideoReleaseBackbuffer();
    VideoGetSize(&W, &H);
    if (W == 0 || H == 0) {
        return;
    }
    Bytes = (UINT64)W * (UINT64)H * sizeof(UINT32);
    Pages = (UINT32)((Bytes + PAGE_SIZE - 1) / PAGE_SIZE);
    if (Pages == 0) {
        return;
    }
    Buf = (UINT32 *)PhysicalMemoryAllocatePages(Pages);
    if (Buf == 0) {
        return;
    }
    VideoSetBackbuffer(Buf, Pages);
    HalIgpuPresentPrepare();
}

int HalVideoCanHotSetMode(void) {
    /* QEMU：Bochs；真机：Boot 交接的 GOP（无 Bochs） */
    if (VideoBochsAvailable()) {
        return 1;
    }
    return VideoGopAvailable();
}

int HalVideoSetMode(UINT32 Width, UINT32 Height) {
    int Rc;

    if (VideoBochsAvailable()) {
        Rc = VideoBochsSetMode(Width, Height);
    } else {
        Rc = VideoGopSetMode(Width, Height);
    }
    if (Rc != 0) {
        return -1;
    }
    HalVideoInitBackbuffer();
    return 0;
}

UINT32 HalVideoModeCount(void) {
    const BOOT_INFO *Info = BootInfoGet();

    if (!Info || Info->VideoModeCount == 0) {
        return 0;
    }
    if (Info->VideoModeCount > BOOT_VIDEO_MODE_MAX) {
        return BOOT_VIDEO_MODE_MAX;
    }
    return Info->VideoModeCount;
}

int HalVideoModeGet(UINT32 Index, UINT32 *Width, UINT32 *Height) {
    const BOOT_INFO *Info = BootInfoGet();

    if (!Info || Index >= Info->VideoModeCount || Index >= BOOT_VIDEO_MODE_MAX) {
        return -1;
    }
    if (Width) {
        *Width = Info->VideoModes[Index].Width;
    }
    if (Height) {
        *Height = Info->VideoModes[Index].Height;
    }
    return 0;
}

UINT64 HalVideoFrameBufferBase(void) {
    return VideoFrameBufferBase();
}

UINT64 HalVideoFrameBufferSize(void) {
    return VideoFrameBufferSize();
}

void HalVideoSetScanout(UINT32 *Va, UINT32 PitchPx, UINT64 Phys, UINT64 Size) {
    VideoSetScanout(Va, PitchPx, Phys, Size);
}

void HalVideoPresent(void) {
    VideoPresent();
}

void HalVideoPresentFlush(void) {
    VideoPresentFlush();
}

void HalVideoMarkDirty(UINT32 X, UINT32 Y, UINT32 W, UINT32 H) {
    DirtyUnion(X, Y, W, H);
}

void HalVideoSetPresentChunkRows(UINT32 Rows) {
    VideoSetPresentChunkRows(Rows);
}

void HalVideoSetGlyphSmooth(int On) {
    VideoSetGlyphSmooth(On);
}

void HalVideoDrawBeginFront(void) {
    VideoDrawBeginFront();
}

void HalVideoDrawEndFront(void) {
    VideoDrawEndFront();
}

int HalVideoBackbufferEnabled(void) {
    return VideoBackbufferEnabled();
}

UINT64 HalVideoBackbufferBase(void) {
    return VideoBackbufferBase();
}

void HalVideoGetSize(UINT32 *Width, UINT32 *Height) {
    VideoGetSize(Width, Height);
}

UINT32 HalVideoGetUiScale(void) {
    return VideoGetUiScale();
}

void HalVideoGetPhysicalSize(UINT32 *Width, UINT32 *Height) {
    VideoGetPhysicalSize(Width, Height);
}

int HalVideoSetUiScale(UINT32 Percent) {
    if (VideoSetUiScale(Percent) != 0) {
        return -1;
    }
    HalVideoInitBackbuffer();
    if (!VideoBackbufferEnabled() && Percent != 100 &&
        VideoGetUiScale() != 100) {
        /* 无后缓冲无法缩放 Present；退回 100% */
        (void)VideoSetUiScale(100);
        HalVideoInitBackbuffer();
        return -1;
    }
    return 0;
}
