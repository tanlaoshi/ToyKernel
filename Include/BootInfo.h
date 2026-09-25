/*
 * BootInfo.h — 各架构 Startup 传给 Common 的通用启动信息
 */
#ifndef BOOT_INFO_H
#define BOOT_INFO_H

#include "BootTypes.h"

#define BOOT_MEMORY_REGIONS_MAX 64
#define BOOT_VIDEO_MODE_MAX     32

typedef struct {
    UINT64 Phys;
    UINT64 Size;
    UINT32 Free; /* 1 = 可分配（Conventional），0 = 保留 */
} BOOT_MEMORY_REGION;

typedef struct {
    UINT32 Width;
    UINT32 Height;
    UINT32 ModeNumber; /* PR-G-hotres-pc：GOP SetMode 下标；0 也可为合法模式 */
} BOOT_VIDEO_MODE;

typedef struct {
    UINT64 FrameBufferBase;
    UINT64 FrameBufferSize;
    UINT32 HorizontalResolution;
    UINT32 VerticalResolution;
    UINT32 PixelsPerScanLine;

    BOOT_MEMORY_REGION Regions[BOOT_MEMORY_REGIONS_MAX];
    UINT32             RegionCount;

    UINT64 KernelStart;
    UINT64 KernelEnd;

    /* PR-G-modes：Boot 传入的可用显示模式（0=未知，Settings 回退硬编码表） */
    UINT32          VideoModeCount;
    BOOT_VIDEO_MODE VideoModes[BOOT_VIDEO_MODE_MAX];
    /* PR-G-hotres-pc：EFI GOP*；0=无交接 / 旧 Boot */
    UINT64          GopProtocol;
} BOOT_INFO;

typedef struct {
    UINT64 FrameBufferBase;
    UINT64 FrameBufferSize;
    UINT32 HorizontalResolution;
    UINT32 VerticalResolution;
    UINT32 PixelsPerScanLine;
} VIDEO_CONFIG;

void BootInfoSet(const BOOT_INFO *Info);
const BOOT_INFO *BootInfoGet(void);

VIDEO_CONFIG BootInfoToVideoConfig(const BOOT_INFO *Info);

#endif
