/*
 * HalVideoDraw.c — PR-S3-halvideo-1：像素 / 矩形 / 文字 / 裁剪
 */
#include "HalVideo.h"
#include "Video.h"

void HalVideoDrawPixel(UINT32 X, UINT32 Y, UINT32 Color) {
    VideoDrawPixel(X, Y, Color);
}

void HalVideoDrawPixelRaw(UINT32 X, UINT32 Y, UINT32 Color) {
    VideoDrawPixelRaw(X, Y, Color);
}

void HalVideoXorPixelRaw(UINT32 X, UINT32 Y, UINT32 Mask) {
    VideoXorPixelRaw(X, Y, Mask);
}

void HalVideoCursorOverlayBegin(void) {
    VideoCursorOverlayBegin();
}

void HalVideoCursorOverlayEnd(void) {
    VideoCursorOverlayEnd();
}

UINT32 HalVideoReadPixel(UINT32 X, UINT32 Y) {
    return VideoReadPixel(X, Y);
}

void HalVideoFillRect(UINT32 X, UINT32 Y, UINT32 Width, UINT32 Height, UINT32 Color) {
    VideoFillRect(X, Y, Width, Height, Color);
}

UINT32 HalVideoBlendRgb(UINT32 Dst, UINT32 Src, UINT8 Alpha) {
    return VideoBlendRgb(Dst, Src, Alpha);
}

void HalVideoBlendPixel(UINT32 X, UINT32 Y, UINT32 Color, UINT8 Alpha) {
    VideoBlendPixel(X, Y, Color, Alpha);
}

void HalVideoBlendPixelRaw(UINT32 X, UINT32 Y, UINT32 Color, UINT8 Alpha) {
    VideoBlendPixelRaw(X, Y, Color, Alpha);
}

void HalVideoBlendFillRect(UINT32 X, UINT32 Y, UINT32 Width, UINT32 Height,
                           UINT32 Color, UINT8 Alpha) {
    VideoBlendFillRect(X, Y, Width, Height, Color, Alpha);
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
