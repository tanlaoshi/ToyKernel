/*

 * HAL/X86_64/HalVideo.c — 帧缓冲门面，委托 GOP 驱动

 */

#include "HalVideo.h"

#include "Video.h"



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



void HalVideoGetSize(UINT32 *Width, UINT32 *Height) {

    VideoGetSize(Width, Height);

}



void HalVideoDrawPixel(UINT32 X, UINT32 Y, UINT32 Color) {

    VideoDrawPixel(X, Y, Color);

}



void HalVideoDrawPixelRaw(UINT32 X, UINT32 Y, UINT32 Color) {

    VideoDrawPixelRaw(X, Y, Color);

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

