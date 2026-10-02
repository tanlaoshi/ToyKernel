/*
 * IgpuScanout.c — igpu-5：双缓冲 + vblank 翻页，避免写入正在扫描的 FB
 *
 * 后缓冲 SRC_COPY → 隐藏缓冲，等帧计数变化后再改 PLANE_SURF。
 * 失败软退：保持固件 gtt0，Present 仍可直写（有撕边）。
 */
#include "Igpu.h"
#include "Hal.h"
#include "HalVideo.h"
#include "PhysicalMemory.h"
#include "VirtualMemory.h"
#include "BootInfo.h"
#include "ToySerialLog.h"

#define IGPU_FLIP0_GTT       0x0A000000ull
#define IGPU_REG_PIPE_FRMC_A 0x70040u /* PIPEA FRMCOUNT（Gen9 族） */
#define IGPU_VBLANK_WAIT_US  40000u

static int gScanOk;
static int gScanLive;
static int gShown; /* 0/1 */
static UINT32 *gVa[2];
static UINT64 gPhys[2];
static UINT64 gGtt[2];
static UINTN gBytes;
static UINT32 gPitchPx;
static UINT32 gWidth;
static UINT32 gHeight;
static UINT32 gSurfReg;
static UINT64 gOrigFb;
static UINT32 *gOrigFront;
static UINT32 gOrigPitch;

int IgpuScanoutOk(void) {
    return gScanOk;
}

UINT64 IgpuScanoutShownGtt(void) {
    if (gScanLive) {
        return gGtt[gShown];
    }
    return (UINT64)IgpuGttSurf();
}

static int MapBuf(UINT64 GttBase, UINT64 PhysBase, UINTN Bytes) {
    UINTN Pages;
    UINTN i;

    Pages = (Bytes + 4095u) / 4096u;
    for (i = 0; i < Pages; i++) {
        if (!IgpuGsmMapQuiet(GttBase + (UINT64)i * 4096ull,
                             PhysBase + (UINT64)i * 4096ull)) {
            return 0;
        }
    }
    IgpuGsmFlush();
    return 1;
}

static void WaitVblank(void) {
    UINT32 Start;
    UINT32 Waited;

    Start = IgpuMmioRead32(IGPU_REG_PIPE_FRMC_A);
    for (Waited = 0; Waited < IGPU_VBLANK_WAIT_US; Waited += 50u) {
        if (IgpuMmioRead32(IGPU_REG_PIPE_FRMC_A) != Start) {
            return;
        }
        IgpuStallUs(50);
    }
}

static int AllocOne(int Idx, UINT64 GttOff) {
    UINT32 Pages;
    void *Buf;
    UINT64 Flags;

    Pages = (UINT32)((gBytes + PAGE_SIZE - 1) / PAGE_SIZE);
    Buf = PhysicalMemoryAllocatePages(Pages);
    if (!Buf) {
        return 0;
    }
    gPhys[Idx] = (UINT64)(UINTN)Buf;
    gVa[Idx] = (UINT32 *)Buf;
    gGtt[Idx] = GttOff;
    Flags = HalVideoFbMapFlags();
    if (VirtualMemoryMapRange(gPhys[Idx], gPhys[Idx], gBytes, Flags) != 0) {
        return 0;
    }
    if (!MapBuf(GttOff, gPhys[Idx], gBytes)) {
        return 0;
    }
    return 1;
}

int IgpuScanoutInit(void) {
    UINT64 Stride;
    const BOOT_INFO *Info;

    if (gScanOk) {
        return 1;
    }
    if (!IgpuReady() || !IgpuGttOk() || !IgpuPresentCopyOk()) {
        return 0;
    }

    HalVideoGetPhysicalSize(&gWidth, &gHeight);
    Info = BootInfoGet();
    gPitchPx = Info ? Info->PixelsPerScanLine : gWidth;
    if (gPitchPx == 0) {
        gPitchPx = gWidth;
    }
    if (gWidth == 0 || gHeight == 0) {
        return 0;
    }
    Stride = (UINT64)gPitchPx * (UINT64)gHeight * 4ull;
    gBytes = (UINTN)Stride;
    gSurfReg = IgpuGttSurfReg();
    gOrigFb = HalVideoFrameBufferBase();
    gOrigFront = (UINT32 *)(UINTN)gOrigFb;
    gOrigPitch = gPitchPx;

    if (!AllocOne(0, IGPU_FLIP0_GTT)) {
        ToyLogBoot("Boot: igpu scanout soft (oom0)\n");
        return 0;
    }
    if (!AllocOne(1, IGPU_FLIP0_GTT + ((gBytes + 0x1FFFFFull) & ~0x1FFFFFull))) {
        ToyLogBoot("Boot: igpu scanout soft (oom1)\n");
        return 0;
    }

    gShown = 0;
    gScanLive = 0;
    gScanOk = 1;
    ToyLogBoot("Boot: igpu scanout flip ready\n");
    return 1;
}

void IgpuScanoutInvalidate(void) {
    if (gScanLive) {
        (void)IgpuForcewakeGet();
        IgpuMmioWrite32(gSurfReg, (UINT32)IgpuGttSurf());
        HalVideoSetScanout(gOrigFront, gOrigPitch, gOrigFb, gBytes);
    }
    gScanLive = 0;
    gScanOk = 0;
}

int IgpuScanoutPresent(UINT64 BackGtt, UINT32 BackPitchPx) {
    int Hidden;
    UINT32 PitchB;
    UINT32 FrontPitchB;

    if (!gScanOk || !IgpuPresentCopyOk()) {
        return 0;
    }
    if (BackGtt == 0 || BackPitchPx == 0 || gWidth == 0 || gHeight == 0) {
        return 0;
    }
    if (!IgpuForcewakeGet()) {
        return 0;
    }

    Hidden = 1 - gShown;
    PitchB = BackPitchPx * 4u;
    FrontPitchB = gPitchPx * 4u;
    __asm__ volatile ("mfence" ::: "memory");
    /* 整屏一次：隐藏缓冲始终是完整帧，翻页无残缺 */
    if (!IgpuSrcCopyRect(0, 0, 0, 0, gWidth, gHeight, PitchB, FrontPitchB,
                         BackGtt, gGtt[Hidden])) {
        return 0;
    }

    WaitVblank();
    IgpuMmioWrite32(gSurfReg, (UINT32)gGtt[Hidden]);
    HalVideoSetScanout(gVa[Hidden], gPitchPx, gPhys[Hidden], gBytes);
    gShown = Hidden;
    gScanLive = 1;
    return 1;
}
