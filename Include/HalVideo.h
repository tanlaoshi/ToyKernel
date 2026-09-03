/*
 * HalVideo.h — 帧缓冲 HAL 门面（Common 经 Hal.h 使用，不直接 include 驱动头）
 */
#ifndef HAL_VIDEO_H
#define HAL_VIDEO_H

#include "BootInfo.h"

void HalVideoSet(const VIDEO_CONFIG *Config);
/* PR-G9：分配并启用后缓冲；Present 提交脏区到 GOP */
void HalVideoInitBackbuffer(void);
void HalVideoPresent(void);
int HalVideoBackbufferEnabled(void);
void HalVideoGetSize(UINT32 *Width, UINT32 *Height);

void HalVideoDrawPixel(UINT32 X, UINT32 Y, UINT32 Color);
/* 忽略客户区 clip（鼠标光标） */
void HalVideoDrawPixelRaw(UINT32 X, UINT32 Y, UINT32 Color);
UINT32 HalVideoReadPixel(UINT32 X, UINT32 Y);
void HalVideoFillRect(UINT32 X, UINT32 Y, UINT32 Width, UINT32 Height, UINT32 Color);
void HalVideoCopyRect(UINT32 SrcX, UINT32 SrcY, UINT32 DstX, UINT32 DstY,
                      UINT32 Width, UINT32 Height);
void HalVideoReadRect(UINT32 X, UINT32 Y, UINT32 Width, UINT32 Height, UINT32 *Out);
void HalVideoWriteRect(UINT32 X, UINT32 Y, UINT32 Width, UINT32 Height, const UINT32 *In);
void HalVideoClearScreen(UINT32 Color);

void HalVideoDrawCharAt(UINT32 X, UINT32 Y, char C, UINT32 Color);
void HalVideoDrawStringAt(UINT32 X, UINT32 Y, const char *Text, UINT32 Color);
void HalVideoDrawChar(char C, UINT32 Color);
void HalVideoDrawString(const char *Text, UINT32 Color);
void HalVideoEraseLastChar(void);

void HalVideoSetClipRegion(UINT32 X, UINT32 Y, UINT32 Width, UINT32 Height, UINT32 Background);
void HalVideoSetClipOrigin(UINT32 X, UINT32 Y, UINT32 Width, UINT32 Height, UINT32 Background);
void HalVideoGetTextCursor(UINT32 *X, UINT32 *Y);
void HalVideoSetTextCursor(UINT32 X, UINT32 Y);
void HalVideoClearClip(void);

#endif
