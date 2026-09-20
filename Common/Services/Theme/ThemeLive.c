/*
 * ThemeLive.c — 运行时改缩放 / 分辨率
 * 核心：Theme.c
 */
#include "Theme.h"
#include "ThemePrivate.h"

int ThemeApplyUiScaleLive(UINT32 Percent) {
    UINT32 Prev = ThemeUiScale();
    UINT32 Next = NormalizeUiScale(Percent);

    gThemeUiScale = Next;
    gScaleUserSet = 1; /* Settings 显式缩放；勿再被 4K 默认 200% 覆盖 */
    if (HalVideoSetUiScale(Next) != 0) {
        gThemeUiScale = Prev;
        (void)HalVideoSetUiScale(Prev);
        return -1;
    }
    GuiOnDisplayResize();
    return 0;
}

int ThemeApplyDisplayLive(UINT32 Width, UINT32 Height) {
    const BOOT_INFO *Info;
    UINT64 Base;
    UINT64 MapBytes;
    UINT64 Need;

    if (Width < 640 || Height < 480) {
        return -1;
    }
    if (!HalVideoCanHotSetMode()) {
        return -1;
    }

    Info = BootInfoGet();
    Base = HalVideoFrameBufferBase();
    if (Base == 0 && Info) {
        Base = Info->FrameBufferBase;
    }
    if (Base == 0) {
        return -1;
    }

    Need = (UINT64)Width * (UINT64)Height * sizeof(UINT32);
    /* 映射到至少 16MiB，覆盖 Settings 最大档 1600x900 */
    MapBytes = 16ull * 1024 * 1024;
    if (Info && Info->FrameBufferSize > MapBytes) {
        MapBytes = Info->FrameBufferSize;
    }
    if (Need > MapBytes) {
        MapBytes = Need;
    }
    if (VirtualMemoryMapRange(Base, Base, (UINTN)MapBytes,
                              HalVideoFbMapFlags()) != 0) {
        return -1;
    }

    if (HalVideoSetMode(Width, Height) != 0) {
        return -1;
    }
    GuiOnDisplayResize();
    return 0;
}
