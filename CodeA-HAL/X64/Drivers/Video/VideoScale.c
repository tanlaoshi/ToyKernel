/*
 * VideoScale.c — UI 缩放 / 逻辑分辨率 / VideoSet（PR-H-video-split-1）
 *
 * 从 Video.c 迁出；只搬家、不改逻辑。
 * VideoSet 与 ApplyLogicalFromPhys 同文件（初始化即算逻辑分辨率）。
 */
#include "VideoPrivate.h"

UINT32 NormalizeUiScale(UINT32 Percent) {
    if (Percent <= 75) {
        return 50;
    }
    if (Percent <= 125) {
        return 100;
    }
    if (Percent <= 175) {
        return 150;
    }
    return 200;
}

/* 逻辑分辨率 = 物理 * 100 / scale；上限约 8M 像素防 OOM */
void ApplyLogicalFromPhys(void) {
    UINT32 Lw;
    UINT32 Lh;
    UINT32 Scale = gUiScale ? gUiScale : 100u;

    if (gPhysW == 0 || gPhysH == 0) {
        return;
    }
    Lw = (gPhysW * 100u) / Scale;
    Lh = (gPhysH * 100u) / Scale;
    if (Lw < 320) {
        Lw = 320;
    }
    if (Lh < 240) {
        Lh = 240;
    }
    while ((UINT64)Lw * (UINT64)Lh > 8ull * 1024ull * 1024ull) {
        Lw = (Lw * 3u) / 4u;
        Lh = (Lh * 3u) / 4u;
        if (Lw < 320 || Lh < 240) {
            Lw = gPhysW;
            Lh = gPhysH;
            gUiScale = 100;
            break;
        }
    }
    gScreen.Width = Lw;
    gScreen.Height = Lh;
}

void VideoSet(VIDEO_CONFIG *VideoConfig) {
    gPhysW = VideoConfig->HorizontalResolution;
    gPhysH = VideoConfig->VerticalResolution;
    gScreen.PixelsPerScanLine = VideoConfig->PixelsPerScanLine;
    gScreen.FrameBufferBase = VideoConfig->FrameBufferBase;
    gScreen.FrameBufferSize = VideoConfig->FrameBufferSize;
    gScreen.CursorX = 0;
    gScreen.CursorY = 0;
    gFront = (UINT32 *)(UINTN)VideoConfig->FrameBufferBase;
    gFrontPitch = VideoConfig->PixelsPerScanLine;
    /* Pitch*H 可能大于 Width*H；Present/映射须按 Pitch 算 */
    if (gFrontPitch != 0 && VideoConfig->VerticalResolution != 0) {
        UINT64 Layout = (UINT64)gFrontPitch *
                        (UINT64)VideoConfig->VerticalResolution * 4ull;
        if (gScreen.FrameBufferSize < Layout) {
            gScreen.FrameBufferSize = Layout;
        }
    }
    gBack = 0;
    gBackPitch = 0;
    gBackPages = 0;
    gBackOn = 0;
    gDirty = 0;
    gCurDirty = 0;
    gUiScale = NormalizeUiScale(gUiScale);
    ApplyLogicalFromPhys();
}

UINT32 VideoGetUiScale(void) {
    return gUiScale ? gUiScale : 100u;
}

void VideoGetPhysicalSize(UINT32 *Width, UINT32 *Height) {
    if (Width) {
        *Width = gPhysW;
    }
    if (Height) {
        *Height = gPhysH;
    }
}

/*
 * 改 UI 缩放并重算逻辑分辨率；释放后缓冲（调用方再 InitBackbuffer）。
 * Percent 归到 50/100/150/200。
 */
int VideoSetUiScale(UINT32 Percent) {
    UINT32 Next = NormalizeUiScale(Percent);

    if (gPhysW == 0 || gPhysH == 0) {
        gUiScale = Next;
        return -1;
    }
    VideoReleaseBackbuffer();
    gUiScale = Next;
    ApplyLogicalFromPhys();
    gDirty = 0;
    gCurDirty = 0;
    return 0;
}

