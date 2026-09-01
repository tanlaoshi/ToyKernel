/*
 * BootInfo.c — 全局启动信息（由 HAL Startup 写入，Common 只读）
 */
#include "BootInfo.h"

static BOOT_INFO gBootInfo;
static int gBootInfoValid;

void BootInfoSet(const BOOT_INFO *Info) {
    if (!Info) {
        gBootInfoValid = 0;
        return;
    }
    gBootInfo = *Info;
    gBootInfoValid = 1;
}

const BOOT_INFO *BootInfoGet(void) {
    if (!gBootInfoValid) {
        return 0;
    }
    return &gBootInfo;
}

VIDEO_CONFIG BootInfoToVideoConfig(const BOOT_INFO *Info) {
    VIDEO_CONFIG V;

    if (!Info) {
        V.FrameBufferBase = 0;
        V.FrameBufferSize = 0;
        V.HorizontalResolution = 0;
        V.VerticalResolution = 0;
        V.PixelsPerScanLine = 0;
        return V;
    }
    V.FrameBufferBase = Info->FrameBufferBase;
    V.FrameBufferSize = Info->FrameBufferSize;
    V.HorizontalResolution = Info->HorizontalResolution;
    V.VerticalResolution = Info->VerticalResolution;
    V.PixelsPerScanLine = Info->PixelsPerScanLine;
    return V;
}
