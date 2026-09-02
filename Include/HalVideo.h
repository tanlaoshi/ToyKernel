/*
 * HalVideo.h — 帧缓冲 HAL 门面（Common 经 Hal.h 使用，不直接 include 驱动头）
 *
 * PR-5b：初始化与分辨率查询；绘制 API 见后续 PR-5c。
 */
#ifndef HAL_VIDEO_H
#define HAL_VIDEO_H

#include "BootInfo.h"

void HalVideoSet(const VIDEO_CONFIG *Config);
void HalVideoGetSize(UINT32 *Width, UINT32 *Height);

#endif
