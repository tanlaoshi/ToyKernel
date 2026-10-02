/*
 * IgpuPresent.c — PR-G-igpu-4：后缓冲映 GGTT + XY_SRC_COPY → gtt0 scanout
 *
 * Dst = 固件 SURF（通常 0）；PTE0 物理 0x8C…。勿把 CPU LFB 0xC0… 当 DMA 目标。
 */
#include "Igpu.h"
#include "Hal.h"
#include "HalVideo.h"
#include "ToySerialLog.h"

#define IGPU_BACK_GTT_OFF    0x04000000ull
#define IGPU_XY_SRC_COPY     ((2u << 29) | (0x53u << 22))
#define IGPU_BLT_WRITE_RGB   (1u << 20)
#define IGPU_BLT_WRITE_ALPHA (1u << 21)
#define IGPU_BR13_DEPTH_32   ((1u << 24) | (1u << 25))
#define IGPU_ROP_SRC_COPY    (0xCCu << 16)
#define IGPU_BLIT_MIN_PX     64u /* 细露出条也走 BCS */
#define IGPU_MAP_MAX_PAGES   8192u

static int gBackMapped;
static UINT64 gBackGtt;
static UINT64 gBackPhys;
static UINTN gBackBytes;
static int gScaleSkipLogged;

int IgpuReady(void) {
    if (HalCpuIsHypervisor()) {
        return 0;
    }
    return IgpuBlitOk();
}

UINT32 IgpuBlitMinPixels(void) {
    return IGPU_BLIT_MIN_PX;
}

void IgpuNotePresentSkipScale(void) {
    if (gScaleSkipLogged || !IgpuReady()) {
        return;
    }
    gScaleSkipLogged = 1;
    ToyLogBoot("Boot: igpu present skip (scale)\n");
}

void IgpuBackInvalidate(void) {
    gBackMapped = 0;
    gBackBytes = 0;
}

UINT64 IgpuBackGttOff(void) {
    return gBackMapped ? gBackGtt : 0;
}

int IgpuBackMap(UINT64 PhysBase, UINTN Bytes) {
    UINTN Pages;
    UINTN i;

    if (!IgpuReady() || PhysBase == 0 || Bytes == 0) {
        return 0;
    }
    if ((PhysBase & 0xFFFu) != 0) {
        return 0;
    }
    if (gBackMapped && gBackPhys == PhysBase && gBackBytes >= Bytes) {
        return 1;
    }
    Pages = (Bytes + 4095u) / 4096u;
    if (Pages == 0 || Pages > IGPU_MAP_MAX_PAGES) {
        ToyLogBoot("Boot: igpu back soft (pages)\n");
        return 0;
    }
    for (i = 0; i < Pages; i++) {
        if (!IgpuGsmMapQuiet(IGPU_BACK_GTT_OFF + (UINT64)i * 4096ull,
                             PhysBase + (UINT64)i * 4096ull)) {
            ToyLogBoot("Boot: igpu back soft (pte)\n");
            gBackMapped = 0;
            return 0;
        }
    }
    IgpuGsmFlush();
    gBackGtt = IGPU_BACK_GTT_OFF;
    gBackPhys = PhysBase;
    gBackBytes = Bytes;
    gBackMapped = 1;
    return 1;
}

int IgpuSrcCopyRect(UINT32 SrcX, UINT32 SrcY, UINT32 DstX, UINT32 DstY,
                    UINT32 W, UINT32 H, UINT32 SrcPitchB, UINT32 DstPitchB,
                    UINT64 SrcGtt, UINT64 DstGtt) {
    UINT32 Words[10];
    UINT32 X2;
    UINT32 Y2;

    if (!IgpuReady() || W == 0 || H == 0) {
        return 0;
    }
    if (SrcPitchB == 0 || DstPitchB == 0) {
        return 0;
    }
    if ((UINT64)W * (UINT64)H < (UINT64)IGPU_BLIT_MIN_PX) {
        return 0;
    }
    X2 = DstX + W;
    Y2 = DstY + H;
    Words[0] = IGPU_XY_SRC_COPY | IGPU_BLT_WRITE_RGB | IGPU_BLT_WRITE_ALPHA | 8u;
    Words[1] = IGPU_ROP_SRC_COPY | IGPU_BR13_DEPTH_32 | (DstPitchB & 0xFFFFu);
    Words[2] = (DstY << 16) | (DstX & 0xFFFFu);
    Words[3] = (Y2 << 16) | (X2 & 0xFFFFu);
    Words[4] = (UINT32)DstGtt;
    Words[5] = (UINT32)(DstGtt >> 32);
    Words[6] = (SrcY << 16) | (SrcX & 0xFFFFu);
    Words[7] = SrcPitchB & 0xFFFFu;
    Words[8] = (UINT32)SrcGtt;
    Words[9] = (UINT32)(SrcGtt >> 32);
    return IgpuBlitEmit(Words, 10);
}

int IgpuPresentRect(const UINT32 *Back, UINT32 BackPitchPx, UINT32 FrontPitchPx,
                    UINT32 BackH, UINT32 X0, UINT32 Y0, UINT32 X1, UINT32 Y1) {
    UINT32 W;
    UINT32 H;
    UINT32 PitchB;
    UINT32 FrontPitchB;
    UINT64 Phys;
    UINTN Bytes;
    UINT64 DstGtt;

    if (!Back || BackPitchPx == 0 || FrontPitchPx == 0 || BackH == 0) {
        return 0;
    }
    if (X0 >= X1 || Y0 >= Y1 || !IgpuReady() || !IgpuPresentCopyOk()) {
        return 0;
    }
    W = X1 - X0;
    H = Y1 - Y0;
    if ((UINT64)W * (UINT64)H < (UINT64)IGPU_BLIT_MIN_PX) {
        return 0;
    }
    Phys = (UINT64)(UINTN)Back;
    Bytes = (UINTN)BackPitchPx * (UINTN)BackH * 4u;
    if (!IgpuBackMap(Phys, Bytes)) {
        return 0;
    }
    DstGtt = IgpuFrontGttBase();
    PitchB = BackPitchPx * 4u;
    FrontPitchB = FrontPitchPx * 4u;
    __asm__ volatile ("mfence" ::: "memory");
    /* 双缓冲整屏翻页：无撕边；失败再脏矩形直写 gtt0 */
    if (IgpuScanoutOk() && IgpuScanoutPresent(gBackGtt, BackPitchPx)) {
        return 1;
    }
    return IgpuSrcCopyRect(X0, Y0, X0, Y0, W, H, PitchB, FrontPitchB,
                           gBackGtt, DstGtt);
}

int IgpuCopyRectBack(const UINT32 *Back, UINT32 PitchPx, UINT32 BufH,
                     UINT32 SrcX, UINT32 SrcY, UINT32 DstX, UINT32 DstY,
                     UINT32 W, UINT32 H) {
    UINT32 PitchB;
    UINT64 Phys;
    UINTN Bytes;

    if (!Back || PitchPx == 0 || BufH == 0 || W == 0 || H == 0) {
        return 0;
    }
    if (!IgpuReady() || !IgpuPresentCopyOk()) {
        return 0;
    }
    if ((UINT64)W * (UINT64)H < (UINT64)IGPU_BLIT_MIN_PX) {
        return 0;
    }
    if (!(DstX + W <= SrcX || SrcX + W <= DstX ||
          DstY + H <= SrcY || SrcY + H <= DstY)) {
        return 0;
    }
    Phys = (UINT64)(UINTN)Back;
    Bytes = (UINTN)PitchPx * (UINTN)BufH * 4u;
    if (!IgpuBackMap(Phys, Bytes)) {
        return 0;
    }
    PitchB = PitchPx * 4u;
    __asm__ volatile ("mfence" ::: "memory");
    return IgpuSrcCopyRect(SrcX, SrcY, DstX, DstY, W, H, PitchB, PitchB,
                           gBackGtt, gBackGtt);
}
