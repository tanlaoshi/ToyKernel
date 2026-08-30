/*
 * Video.h — 帧缓冲文本与像素绘制
 *
 * 依赖 UEFI GOP 提供的线性帧缓冲。使用 FontData.h 中的 8×16 点阵字体。
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

void SetVideo(VIDEO_CONFIG *VideoConfig);
void VideoGetSize(UINT32 *Width, UINT32 *Height);
void DrawPixel(UINT32 X, UINT32 Y, UINT32 Color);
UINT32 VideoReadPixel(UINT32 X, UINT32 Y);
void VideoFillRect(UINT32 X, UINT32 Y, UINT32 Width, UINT32 Height, UINT32 Color);
void ClearScreen(UINT32 Color);
void DrawCharAt(UINT32 X, UINT32 Y, char C, UINT32 Color);
void DrawStringAt(UINT32 X, UINT32 Y, const char *Text, UINT32 Color);
void DrawChar(char c, UINT32 Color);
void DrawString(const char *Text, UINT32 Color);
void NewLine(void);
void EraseLastChar(void);
void VideoSetClipRegion(UINT32 X, UINT32 Y, UINT32 W, UINT32 H, UINT32 Bg);
void VideoSetClipOrigin(UINT32 X, UINT32 Y, UINT32 W, UINT32 H, UINT32 Bg);
void VideoGetTextCursor(UINT32 *X, UINT32 *Y);
void VideoSetTextCursor(UINT32 X, UINT32 Y);
void VideoClearClip(void);

#endif
