/*
 * KernelModulesInitVideo.c — InitializeVideo（PR-K-seq-4）
 *
 * 人话：把帧缓冲、字体、主题和核显/声卡探针按顺序拉起来。无卡则软退。
 * 从哪读：InitializeVideo。勿在此全屏 Clear（接 Boot 已滚的黑底）。
 */
#include "KernelModulesPrivate.h"
#include "BootInfo.h"
#include "Hal.h"
#include "Font.h"
#include "Theme.h"

static void KernelModulesInitVideoIgpuAudio(void) {
    HalIgpuMmioInitialize();
    HalHdaMmioInitialize();
    HalHdaCodecInitialize();
    HalIgpuForcewakeInitialize();
    HalHdaStreamInitialize();
    HalIgpuGttInitialize();
    HalIgpuBlitInitialize();
    HalIgpuPresentPrepare();
}

int InitializeVideo(void) {
    const BOOT_INFO *Info = BootInfoGet();
    VIDEO_CONFIG Video = BootInfoToVideoConfig(Info);

    FontInitialize();
    ThemeInitialize();
    HalVideoSet(&Video);
    HalVideoEnableFbWc();
    HalVideoInitializeBackbuffer();
    HalSerialGopEnable();
    HalVideoLogFbPte();
    KernelModulesInitVideoIgpuAudio();
    return 0;
}
