/*
 * HAL/Virt/HalVideo.c — 帧缓冲门面，委托 Drivers/Video（PR-V2 ramfb scanout）
 */
#include "HalVideo.h"
#include "Video.h"
#include "PhysicalMemory.h"
#include "Hal.h"

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

void HalVideoInitBackbuffer(void) {
    UINT32 W;
    UINT32 H;
    UINT64 Bytes;
    UINT32 Pages;
    UINT32 *Buf;

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
}

int HalVideoCanHotSetMode(void) {
    return 0;
}

int HalVideoSetMode(UINT32 Width, UINT32 Height) {
    (void)Width;
    (void)Height;
    return -1;
}

UINT64 HalVideoFrameBufferBase(void) {
    return VideoFrameBufferBase();
}

UINT64 HalVideoFrameBufferSize(void) {
    return VideoFrameBufferSize();
}

/* PR-G-fb-pte：x86 才有 PWT/PCD；virt 空实现 */
void HalVideoLogFbPte(void) {
}

int HalVideoFbPteLine(char *Buf, UINTN Max) {
    if (Buf && Max > 0) {
        Buf[0] = 0;
    }
    return 0;
}

void HalVideoEnableFbWc(void) {
}

UINT64 HalVideoFbMapFlags(void) {
    return HAL_PAGE_PRESENT | HAL_PAGE_WRITABLE;
}

void HalVideoPresent(void) {
    VideoPresent();
}

void HalVideoDrawBeginFront(void) {
}

void HalVideoDrawEndFront(void) {
}

int HalVideoBackbufferEnabled(void) {
    return VideoBackbufferEnabled();
}

void HalVideoGetSize(UINT32 *Width, UINT32 *Height) {
    VideoGetSize(Width, Height);
}

void HalVideoDrawPixel(UINT32 X, UINT32 Y, UINT32 Color) {
    VideoDrawPixel(X, Y, Color);
}

void HalVideoDrawPixelRaw(UINT32 X, UINT32 Y, UINT32 Color) {
    VideoDrawPixelRaw(X, Y, Color);
}

void HalVideoXorPixelRaw(UINT32 X, UINT32 Y, UINT32 Mask) {
    VideoXorPixelRaw(X, Y, Mask);
}

UINT32 HalVideoReadPixel(UINT32 X, UINT32 Y) {
    return VideoReadPixel(X, Y);
}

void HalVideoFillRect(UINT32 X, UINT32 Y, UINT32 Width, UINT32 Height, UINT32 Color) {
    VideoFillRect(X, Y, Width, Height, Color);
}

void HalVideoCopyRect(UINT32 SrcX, UINT32 SrcY, UINT32 DstX, UINT32 DstY,
                      UINT32 Width, UINT32 Height) {
    VideoCopyRect(SrcX, SrcY, DstX, DstY, Width, Height);
}

void HalVideoReadRect(UINT32 X, UINT32 Y, UINT32 Width, UINT32 Height, UINT32 *Out) {
    VideoReadRect(X, Y, Width, Height, Out);
}

void HalVideoWriteRect(UINT32 X, UINT32 Y, UINT32 Width, UINT32 Height, const UINT32 *In) {
    VideoWriteRect(X, Y, Width, Height, In);
}

void HalVideoClearScreen(UINT32 Color) {
    VideoClearScreen(Color);
}

void HalVideoDrawCharAt(UINT32 X, UINT32 Y, char C, UINT32 Color) {
    VideoDrawCharAt(X, Y, C, Color);
}

void HalVideoDrawCodepointAt(UINT32 X, UINT32 Y, UINT32 Cp, UINT32 Color) {
    VideoDrawCodepointAt(X, Y, Cp, Color);
}

void HalVideoDrawStringAt(UINT32 X, UINT32 Y, const char *Text, UINT32 Color) {
    VideoDrawStringAt(X, Y, Text, Color);
}

void HalVideoDrawChar(char C, UINT32 Color) {
    VideoDrawChar(C, Color);
}

void HalVideoDrawString(const char *Text, UINT32 Color) {
    VideoDrawString(Text, Color);
}

void HalVideoEraseLastChar(void) {
    VideoEraseLastChar();
}

void HalVideoSetClipRegion(UINT32 X, UINT32 Y, UINT32 Width, UINT32 Height, UINT32 Background) {
    VideoSetClipRegion(X, Y, Width, Height, Background);
}

void HalVideoSetClipOrigin(UINT32 X, UINT32 Y, UINT32 Width, UINT32 Height, UINT32 Background) {
    VideoSetClipOrigin(X, Y, Width, Height, Background);
}

void HalVideoGetTextCursor(UINT32 *X, UINT32 *Y) {
    VideoGetTextCursor(X, Y);
}

void HalVideoSetTextCursor(UINT32 X, UINT32 Y) {
    VideoSetTextCursor(X, Y);
}

void HalVideoClearClip(void) {
    VideoClearClip();
}

void HalVideoScrollClipLines(int Delta) {
    VideoScrollClipLines(Delta);
}
