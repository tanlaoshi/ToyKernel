/*
 * Video.h — 帧缓冲文本与像素绘制
 *
 * 依赖 UEFI GOP 提供的线性帧缓冲。使用 FontData.h 中的 Terminus 16×32 点阵字体。
 */
#ifndef VIDEO_H
#define VIDEO_H

#include "BootConfig.h"

/* 当前屏幕状态（分辨率、帧缓冲地址、文本光标） */
typedef struct {
    UINT32 Width;
    UINT32 Height;
    UINT32 PixelsPerScanLine;
    UINT64 FrameBufferBase;
    UINT64 FrameBufferSize;
    UINT32 CursorX;
    UINT32 CursorY;
} SCREEN_INFO;

void VideoSet(VIDEO_CONFIG *VideoConfig);
void VideoGetSize(UINT32 *Width, UINT32 *Height);
void VideoDrawPixel(UINT32 X, UINT32 Y, UINT32 Color);
UINT32 VideoReadPixel(UINT32 X, UINT32 Y);
void VideoFillRect(UINT32 X, UINT32 Y, UINT32 Width, UINT32 Height, UINT32 Color);
void VideoCopyRect(UINT32 SrcX, UINT32 SrcY, UINT32 DstX, UINT32 DstY,
                   UINT32 Width, UINT32 Height);
void VideoReadRect(UINT32 X, UINT32 Y, UINT32 Width, UINT32 Height, UINT32 *Out);
void VideoWriteRect(UINT32 X, UINT32 Y, UINT32 Width, UINT32 Height, const UINT32 *In);
void VideoClearScreen(UINT32 Color);
void VideoDrawCharAt(UINT32 X, UINT32 Y, char C, UINT32 Color);
void VideoDrawStringAt(UINT32 X, UINT32 Y, const char *Text, UINT32 Color);
void VideoDrawChar(char c, UINT32 Color);
void VideoDrawString(const char *Text, UINT32 Color);
void VideoNewLine(void);
void VideoEraseLastChar(void);
void VideoSetClipRegion(UINT32 X, UINT32 Y, UINT32 Width, UINT32 Height, UINT32 Background);
void VideoSetClipOrigin(UINT32 X, UINT32 Y, UINT32 Width, UINT32 Height, UINT32 Background);
void VideoGetTextCursor(UINT32 *X, UINT32 *Y);
void VideoSetTextCursor(UINT32 X, UINT32 Y);
void VideoClearClip(void);

#endif
