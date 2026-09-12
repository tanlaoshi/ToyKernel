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
void HalVideoDrawBeginFront(void);
void HalVideoDrawEndFront(void);
int HalVideoBackbufferEnabled(void);
void HalVideoGetSize(UINT32 *Width, UINT32 *Height);
/* UI 整体缩放（50/100/150/200）；成功 0，重配后缓冲 */
UINT32 HalVideoGetUiScale(void);
void HalVideoGetPhysicalSize(UINT32 *Width, UINT32 *Height);
int HalVideoSetUiScale(UINT32 Percent);
/* PR-G-hotres：QEMU/Bochs VGA 可热切则 1；真机/无 DISPI 为 0 */
int HalVideoCanHotSetMode(void);
/*
 * PR-G-hotres：运行中改分辨率（Bochs DISPI）。成功 0。
 * 调用方先 VirtualMemoryMapRange 覆盖足够大的 LFB。
 */
int HalVideoSetMode(UINT32 Width, UINT32 Height);
UINT64 HalVideoFrameBufferBase(void);
UINT64 HalVideoFrameBufferSize(void);
/* PR-G-fb-pte：boot 一行 FB phys + PWT/PCD(/PAT) + 推导 cache；不改映射。x86 有内容，其它 HAL 空实现 */
void HalVideoLogFbPte(void);
/* 填入一行（无尾 '\n'）；成功返回长度，无 FB 返回 0。供 PHOTO 直绘 */
int HalVideoFbPteLine(char *Buf, UINTN Max);
/*
 * PR-G-fb-wc：PAT 开 WC 后仅重映 LFB（PWT→WC）；MMIO/xHCI 不动。
 * 映射标志供 Theme 热切等复用。
 */
void HalVideoEnableFbWc(void);
UINT64 HalVideoFbMapFlags(void);

void HalVideoDrawPixel(UINT32 X, UINT32 Y, UINT32 Color);
/* 忽略客户区 clip（鼠标光标） */
void HalVideoDrawPixelRaw(UINT32 X, UINT32 Y, UINT32 Color);
void HalVideoXorPixelRaw(UINT32 X, UINT32 Y, UINT32 Mask);
/* 光标 save-under / 实心绘制：脏区走光标矩形，避免与 Shell 并成近全屏 */
void HalVideoCursorOverlayBegin(void);
void HalVideoCursorOverlayEnd(void);
UINT32 HalVideoReadPixel(UINT32 X, UINT32 Y);
void HalVideoFillRect(UINT32 X, UINT32 Y, UINT32 Width, UINT32 Height, UINT32 Color);
void HalVideoCopyRect(UINT32 SrcX, UINT32 SrcY, UINT32 DstX, UINT32 DstY,
                      UINT32 Width, UINT32 Height);
void HalVideoReadRect(UINT32 X, UINT32 Y, UINT32 Width, UINT32 Height, UINT32 *Out);
void HalVideoWriteRect(UINT32 X, UINT32 Y, UINT32 Width, UINT32 Height, const UINT32 *In);
void HalVideoClearScreen(UINT32 Color);

void HalVideoDrawCharAt(UINT32 X, UINT32 Y, char C, UINT32 Color);
void HalVideoDrawCodepointAt(UINT32 X, UINT32 Y, UINT32 Cp, UINT32 Color);
void HalVideoDrawStringAt(UINT32 X, UINT32 Y, const char *Text, UINT32 Color);
void HalVideoDrawChar(char C, UINT32 Color);
void HalVideoDrawString(const char *Text, UINT32 Color);
void HalVideoEraseLastChar(void);

void HalVideoSetClipRegion(UINT32 X, UINT32 Y, UINT32 Width, UINT32 Height, UINT32 Background);
void HalVideoSetClipOrigin(UINT32 X, UINT32 Y, UINT32 Width, UINT32 Height, UINT32 Background);
void HalVideoGetTextCursor(UINT32 *X, UINT32 *Y);
void HalVideoSetTextCursor(UINT32 X, UINT32 Y);
void HalVideoClearClip(void);

/* PR-I2：焦点客户区按行滚动（Shell 滚轮） */
void HalVideoScrollClipLines(int Delta);

#endif
