/*
 * BootInfo.h — 各架构 Startup 传给 Common 的通用启动信息
 */
#ifndef BOOT_INFO_H
#define BOOT_INFO_H

#include "BootTypes.h"

#define BOOT_MEMORY_REGIONS_MAX 64

typedef struct {
    UINT64 Phys;
    UINT64 Size;
    UINT32 Free; /* 1 = 可分配（Conventional），0 = 保留 */
} BOOT_MEMORY_REGION;

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
