/*
 * IgpuPresentPrep.c — 启动期映后缓冲 + 屏外 SRC_COPY 探针（igpu-4 / igpu-6）
 *
 * igpu-6：scratch 页 XY_SRC_COPY，CPU 读回成功才置 PresentCopyOk。
 * 不写 GOP / 不改 PLANE_SURF（igpu-7 才接 ScanoutInit）。
 */
#include "Igpu.h"
#include "Hal.h"
#include "HalVideo.h"
#include "BootInfo.h"
#include "PhysicalMemory.h"
#include "VirtualMemory.h"
#include "ToySerialLog.h"
#include "IgpuBlitPrivate.h"

#ifndef PTE_PWT
#define PTE_PWT HAL_PAGE_PWT
#define PTE_PCD HAL_PAGE_PCD
#endif

#define IGPU_PROBE_GTT_SRC  0x05800000ull /* 勿复用 SCRATCH：mem 自检品红仍占 0x02000000 */
#define IGPU_PROBE_GTT_DST  0x05801000ull
#define IGPU_PROBE_W        64u
#define IGPU_PROBE_H        16u
#define IGPU_PROBE_PITCH_B  IGPU_MEM_PITCH
#define IGPU_PROBE_COLOR    0x00C0FFEEu

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

/* 屏外两页：独立 GTT；COLOR_BLT 写 Src 再 SRC_COPY→Dst。不碰 scanout / scratch。 */
static int ProbeMapPage(UINT64 GttOff, UINT64 Phys) {
    UINT64 Pte;

    if (!IgpuGsmMap(GttOff, Phys)) {
        return 0;
    }
    Pte = IgpuGsmPteRead(GttOff);
    if ((Pte & ~0xFFFull) != (Phys & ~0xFFFull) || (Pte & 1ull) == 0) {
        return 0;
    }
    return 1;
}

static int ProbeSrcCopyOffscreen(void) {
    void *SrcPage;
    void *DstPage;
    volatile UINT32 *Src;
    volatile UINT32 *Dst;
    UINT64 SrcPhys;
    UINT64 DstPhys;
    UINT64 FlagsUc;
    UINT32 i;
    UINT32 N;
    UINT32 Last;
    UINT32 Words[8];
    UINT32 Mode;

    SrcPage = PhysicalMemoryAllocatePage();
    DstPage = PhysicalMemoryAllocatePage();
    if (!SrcPage || !DstPage) {
        ToyLogBoot("Boot: igpu copy soft (oom)\n");
        return 0;
    }
    SrcPhys = (UINT64)(UINTN)SrcPage;
    DstPhys = (UINT64)(UINTN)DstPage;
    FlagsUc = PTE_PRESENT | PTE_WRITABLE | PTE_PWT | PTE_PCD;
    if (VirtualMemoryMapRange(SrcPhys, SrcPhys, PAGE_SIZE, FlagsUc) != 0 ||
        VirtualMemoryMapRange(DstPhys, DstPhys, PAGE_SIZE, FlagsUc) != 0) {
        ToyLogBoot("Boot: igpu copy soft (uc)\n");
        return 0;
    }
    Src = (volatile UINT32 *)(UINTN)SrcPhys;
    Dst = (volatile UINT32 *)(UINTN)DstPhys;
    N = PAGE_SIZE / 4u;
    for (i = 0; i < N; i++) {
        Src[i] = 0xA5A5A5A5u;
        Dst[i] = 0xA5A5A5A5u;
    }
    __asm__ volatile ("mfence" ::: "memory");
    FlushCpu((const void *)(UINTN)SrcPhys, PAGE_SIZE);
    FlushCpu((const void *)(UINTN)DstPhys, PAGE_SIZE);
    if (!ProbeMapPage(IGPU_PROBE_GTT_SRC, SrcPhys) ||
        !ProbeMapPage(IGPU_PROBE_GTT_DST, DstPhys)) {
        ToyLogBoot("Boot: igpu copy soft (pte)\n");
        return 0;
    }

    Mode = IgpuMmioRead32(IGPU_RING_MODE);
    if ((Mode & IGPU_GFX_PPGTT_EN) != 0) {
        IgpuMmioWrite32(IGPU_RING_MODE, IGPU_FW_DISABLE(IGPU_GFX_PPGTT_EN));
    }
    if (!IgpuForcewakeGet()) {
        ToyLogBoot("Boot: igpu copy soft (fw)\n");
        return 0;
    }

    /* 与 mem 自检相同：GPU COLOR_BLT 写源，避免 CPU 填的页 GPU 看不见 */
    Words[0] = IGPU_XY_COLOR_NOLEN | IGPU_BLT_WRITE_RGB | IGPU_BLT_WRITE_ALPHA | 5u;
    Words[1] = IGPU_ROP_COLOR_COPY | IGPU_BR13_DEPTH_32 | (IGPU_PROBE_PITCH_B & 0xFFFFu);
    Words[2] = 0;
    Words[3] = (IGPU_PROBE_H << 16) | IGPU_PROBE_W;
    Words[4] = (UINT32)IGPU_PROBE_GTT_SRC;
    Words[5] = 0;
    Words[6] = IGPU_PROBE_COLOR;
    Words[7] = IGPU_MI_NOOP;
    if (!EmitDwords(Words, 7)) {
        ToyLogBoot("Boot: igpu copy soft (color)\n");
        return 0;
    }
    FlushCpu((const void *)(UINTN)SrcPhys, PAGE_SIZE);
    if ((Src[0] & 0x00FFFFFFu) != (IGPU_PROBE_COLOR & 0x00FFFFFFu)) {
        ToyLogBoot("Boot: igpu copy soft (color pix=");
        ToyLogBootHex32(Src[0]);
        ToyLogBoot(")\n");
        return 0;
    }

    if (!IgpuSrcCopyRect(0, 0, 0, 0, IGPU_PROBE_W, IGPU_PROBE_H,
                         IGPU_PROBE_PITCH_B, IGPU_PROBE_PITCH_B,
                         IGPU_PROBE_GTT_SRC, IGPU_PROBE_GTT_DST)) {
        ToyLogBoot("Boot: igpu copy soft (emit)\n");
        return 0;
    }
    FlushCpu((const void *)(UINTN)DstPhys, PAGE_SIZE);
    Last = (IGPU_PROBE_H - 1u) * (IGPU_PROBE_PITCH_B / 4u);
    if ((Dst[0] & 0x00FFFFFFu) != (IGPU_PROBE_COLOR & 0x00FFFFFFu) ||
        (Dst[IGPU_PROBE_W - 1u] & 0x00FFFFFFu) != (IGPU_PROBE_COLOR & 0x00FFFFFFu) ||
        (Dst[Last] & 0x00FFFFFFu) != (IGPU_PROBE_COLOR & 0x00FFFFFFu)) {
        ToyLogBoot("Boot: igpu copy soft (miss pix=");
        ToyLogBootHex32(Dst[0]);
        ToyLogBoot(")\n");
        return 0;
    }
    return 1;
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

    if (!ProbeSrcCopyOffscreen()) {
        ToyLogBoot("Boot: igpu present cpu fallback (probe miss)\n");
        return;
    }
    gPresentCopyOk = 1;
    ToyLogBoot("Boot: igpu present copy ok\n");
}
