/*
 * IgpuPresentPrep.c — 启动期映后缓冲（igpu-4）
 *
 * PR-G-igpu-corner：取消右上角品红 SRC_COPY 探针；不在屏上留测色块。
 * 无探针则不启用 GPU present（cpu fallback），避免误开未验证路径。
 */
#include "Igpu.h"
#include "Hal.h"
#include "HalVideo.h"
#include "BootInfo.h"
#include "ToySerialLog.h"

static int gPresentLogged;
static int gPresentCopyOk;

int IgpuPresentCopyOk(void) {
    return gPresentCopyOk;
}

UINT64 IgpuFrontGttBase(void) {
    return (UINT64)IgpuGttSurf();
}

void IgpuPresentInvalidate(void) {
    gPresentLogged = 0;
    gPresentCopyOk = 0;
    IgpuScanoutInvalidate();
    IgpuBackInvalidate();
}

int IgpuFrontMapEnsure(UINT64 PhysBase, UINTN Bytes) {
    (void)PhysBase;
    (void)Bytes;
    return IgpuGttOk() ? 1 : 0;
}

void IgpuPresentPrepare(void) {
    UINT64 Fb;
    UINT64 Back;
    UINT32 Bw;
    UINT32 Bh;
    UINTN BackBytes;

    if (!IgpuReady() || gPresentLogged) {
        return;
    }
    gPresentLogged = 1;
    gPresentCopyOk = 0;

    if (HalVideoGetUiScale() != 100u) {
        IgpuNotePresentSkipScale();
        return;
    }
    if (!IgpuGttOk()) {
        ToyLogBoot("Boot: igpu present soft (no gtt)\n");
        return;
    }

    Fb = HalVideoFrameBufferBase();
    if (!HalVideoBackbufferEnabled()) {
        ToyLogBoot("Boot: igpu present soft (no back)\n");
        return;
    }
    Back = HalVideoBackbufferBase();
    HalVideoGetSize(&Bw, &Bh);
    if (Back == 0 || Bw == 0 || Bh == 0) {
        ToyLogBoot("Boot: igpu present soft (no back)\n");
        return;
    }
    BackBytes = (UINTN)Bw * (UINTN)Bh * 4u;
    if (!IgpuBackMap(Back, BackBytes)) {
        return;
    }

    ToyLogBoot("Boot: igpu present ready fb=");
    ToyLogBootHex32((UINT32)Fb);
    ToyLogBoot(" back=");
    ToyLogBootHex32((UINT32)Back);
    ToyLogBoot("\n");
    /* 无屏上探针：保持 cpu present */
    ToyLogBoot("Boot: igpu present cpu fallback (no probe)\n");
}
