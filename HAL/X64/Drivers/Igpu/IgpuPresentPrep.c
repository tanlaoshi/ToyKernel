/*
 * IgpuPresentPrep.c — 启动期映后缓冲，SRC_COPY→gtt0 探针（igpu-4）
 *
 * NUC：CPU LFB=0xC0… 与 PTE0 物理 0x8C… 对 CPU 可读回（COLOR_BLT→gtt0 已证）；
 * GPU DMA 不能把 0xC0… 当目标——映 GTT→C0 的探针必黑。Dst 必须用固件 SURF/gtt0。
 */
#include "Igpu.h"
#include "Hal.h"
#include "HalVideo.h"
#include "BootInfo.h"
#include "ToySerialLog.h"

#define IGPU_PROBE_W     64u
#define IGPU_PROBE_H     16u
#define IGPU_PROBE_COLOR 0x00FF00FFu

static int gPresentLogged;
static int gPresentCopyOk;

int IgpuPresentCopyOk(void) {
    return gPresentCopyOk;
}

UINT64 IgpuFrontGttBase(void) {
    /* 与 COLOR_BLT 同目标：固件 scanout（PTE0→0x8C…） */
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

static void FlushCpu(const void *Ptr, UINTN Size) {
    const UINT8 *P = (const UINT8 *)Ptr;
    UINTN Off;

    for (Off = 0; Off < Size; Off += 64) {
        __asm__ volatile ("clflush (%0)" : : "r"(P + Off) : "memory");
    }
    __asm__ volatile ("mfence" ::: "memory");
}

static void ProbeSrcCopy(UINT32 *Back, UINT32 BackPitch, UINT32 FrontPitch,
                         UINT32 Bw, UINT32 Bh) {
    UINT32 X0;
    UINT32 Y0;
    UINT32 X;
    UINT32 Y;
    UINT64 Fb;
    UINT64 DstGtt;
    volatile UINT32 *Front;
    UINT32 Pix;

    if (Bw < IGPU_PROBE_W + 8u || Bh < IGPU_PROBE_H + 8u) {
        ToyLogBoot("Boot: igpu present soft (probe geom)\n");
        return;
    }
    X0 = Bw - IGPU_PROBE_W - 8u;
    Y0 = 8u;
    DstGtt = IgpuFrontGttBase();
    for (Y = 0; Y < IGPU_PROBE_H; Y++) {
        for (X = 0; X < IGPU_PROBE_W; X++) {
            Back[(UINTN)(Y0 + Y) * BackPitch + X0 + X] = IGPU_PROBE_COLOR;
        }
        FlushCpu(&Back[(UINTN)(Y0 + Y) * BackPitch + X0], IGPU_PROBE_W * 4u);
    }
    if (!IgpuSrcCopyRect(X0, Y0, X0, Y0, IGPU_PROBE_W, IGPU_PROBE_H,
                         BackPitch * 4u, FrontPitch * 4u,
                         IgpuBackGttOff(), DstGtt)) {
        ToyLogBoot("Boot: igpu present soft (probe emit)\n");
        return;
    }
    IgpuStallUs(500);
    Fb = HalVideoFrameBufferBase();
    Front = (volatile UINT32 *)(UINTN)Fb;
    FlushCpu((const void *)(UINTN)(Fb + ((UINT64)(Y0 + 8u) * FrontPitch + X0 + 8u) * 4ull),
             64);
    Pix = Front[(Y0 + 8u) * FrontPitch + X0 + 8u];
    ToyLogBoot("Boot: igpu present pix=");
    ToyLogBootHex32(Pix);
    ToyLogBoot(" dst=");
    ToyLogBootHex32((UINT32)DstGtt);
    ToyLogBoot("\n");
    if ((Pix & 0x00FFFFFFu) != (IGPU_PROBE_COLOR & 0x00FFFFFFu)) {
        ToyLogBoot("Boot: igpu present soft (probe miss)\n");
        gPresentCopyOk = 0;
        return;
    }
    gPresentCopyOk = 1;
    ToyLogBoot("Boot: igpu present copy ok\n");
}

void IgpuPresentPrepare(void) {
    UINT64 Fb;
    UINT64 Back;
    UINT32 Bw;
    UINT32 Bh;
    UINT32 FrontPitch;
    UINTN BackBytes;
    const BOOT_INFO *Info;

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
    Info = BootInfoGet();
    FrontPitch = Info ? Info->PixelsPerScanLine : 0;
    HalVideoGetPhysicalSize(&Bw, &Bh);
    if (FrontPitch == 0) {
        FrontPitch = Bw;
    }

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

    ProbeSrcCopy((UINT32 *)(UINTN)Back, Bw, FrontPitch, Bw, Bh);
    if (!gPresentCopyOk) {
        ToyLogBoot("Boot: igpu present cpu fallback\n");
        return;
    }
    if (!IgpuScanoutInit()) {
        ToyLogBoot("Boot: igpu present cpu (no flip)\n");
    }
}
