/*
 * IgpuBlitTest.c — BCS mem/色块自检（PR-S3-igpublit-1）
 */
#include "Igpu.h"
#include "PhysicalMemory.h"
#include "BootInfo.h"
#include "HalVideo.h"
#include "VirtualMemory.h"
#include "Hal.h"
#include "IgpuBlitPrivate.h"

#ifndef PTE_PWT
#define PTE_PWT HAL_PAGE_PWT
#define PTE_PCD HAL_PAGE_PCD
#endif

static void LogBcsFault(void) {
    ToyLogBoot("Boot: igpu bcs ipehr=");
    ToyLogBootHex32(IgpuMmioRead32(IGPU_RING_IPEHR));
    ToyLogBoot(" ipeir=");
    ToyLogBootHex32(IgpuMmioRead32(IGPU_RING_IPEIR));
    ToyLogBoot(" eir=");
    ToyLogBootHex32(IgpuMmioRead32(IGPU_RING_EIR));
    ToyLogBoot(" head=");
    ToyLogBootHex32(IgpuMmioRead32(IGPU_RING_HEAD));
    ToyLogBoot(" tail=");
    ToyLogBootHex32(IgpuMmioRead32(IGPU_RING_TAIL));
    ToyLogBoot("\n");
}

/*
 * B13：batch 暂搁（B12：BB_START 后 HEAD 卡死、IPEHR=垃圾）。
 * 在真 WaitHead（64B 对齐）下重测 ring COLOR——B4～B7 可能被假完成污染。
 */
int SubmitMemTest(void) {
    void *Page;
    volatile UINT32 *Pix;
    UINT32 i;
    UINT32 Words[8];
    UINT32 Sample;
    UINT32 Fence;
    UINT32 Want;
    UINT64 Phys;
    UINT64 Pte;
    UINT64 FlagsUc;

    Page = PhysicalMemoryAllocatePage();
    if (!Page) {
        ToyLogBoot("Boot: igpu mem soft (oom)\n");
        return 0;
    }
    Phys = (UINT64)(UINTN)Page;
    FlagsUc = PTE_PRESENT | PTE_WRITABLE | PTE_PWT | PTE_PCD;
    if (VirtualMemoryMapRange(Phys, Phys, PAGE_SIZE, FlagsUc) != 0) {
        ToyLogBoot("Boot: igpu mem soft (uc)\n");
        return 0;
    }
    Pix = (volatile UINT32 *)(UINTN)Phys;
    for (i = 0; i < (PAGE_SIZE / 4u); i++) {
        Pix[i] = 0xA5A5A5A5u;
    }
    __asm__ volatile ("mfence" ::: "memory");

    if (!IgpuGsmMap(IGPU_SCRATCH_GTT_OFF, Phys)) {
        ToyLogBoot("Boot: igpu mem soft (pte)\n");
        return 0;
    }
    Pte = IgpuGsmPteRead(IGPU_SCRATCH_GTT_OFF);
    ToyLogBoot("Boot: igpu mem pte=");
    ToyLogBootHex32((UINT32)(Pte >> 32));
    ToyLogBoot(":");
    ToyLogBootHex32((UINT32)Pte);
    ToyLogBoot("\n");
    if ((Pte & ~0xFFFull) != (Phys & ~0xFFFull) || (Pte & 1ull) == 0) {
        ToyLogBoot("Boot: igpu mem soft (pte mismatch)\n");
        return 0;
    }

    Want = IGPU_TEST_COLOR;
    Words[0] = (0x20u << 23) | (1u << 22) | 3u;
    Words[1] = (UINT32)IGPU_SCRATCH_GTT_OFF;
    Words[2] = 0;
    Words[3] = Want;
    if (!EmitDwords(Words, 4)) {
        ToyLogBoot("Boot: igpu mem soft (store to)\n");
        return 0;
    }
    Sample = Pix[0];
    ToyLogBoot("Boot: igpu mem store=");
    ToyLogBootHex32(Sample);
    ToyLogBoot("\n");
    if (Sample != Want) {
        ToyLogBoot("Boot: igpu mem soft (store miss)\n");
        return 0;
    }

    {
        UINT32 Mode = IgpuMmioRead32(IGPU_RING_MODE);
        ToyLogBoot("Boot: igpu bcs mode=");
        ToyLogBootHex32(Mode);
        ToyLogBoot("\n");
        if ((Mode & IGPU_GFX_PPGTT_EN) != 0) {
            IgpuMmioWrite32(IGPU_RING_MODE, IGPU_FW_DISABLE(IGPU_GFX_PPGTT_EN));
        }
    }

    if (!IgpuForcewakeGet()) {
        ToyLogBoot("Boot: igpu mem soft (fw)\n");
        return 0;
    }

    /* 哨兵 → COLOR_BLT → MI_STORE fence@+4（同环、真等 HEAD） */
    for (i = 0; i < (PAGE_SIZE / 4u); i++) {
        Pix[i] = 0xA5A5A5A5u;
    }
    __asm__ volatile ("mfence" ::: "memory");

    Words[0] = IGPU_XY_COLOR_NOLEN | IGPU_BLT_WRITE_RGB | IGPU_BLT_WRITE_ALPHA | 5u;
    Words[1] = IGPU_ROP_COLOR_COPY | IGPU_BR13_DEPTH_32 | (IGPU_MEM_PITCH & 0xFFFFu);
    Words[2] = 0;
    Words[3] = (IGPU_MEM_H << 16) | IGPU_MEM_W;
    Words[4] = (UINT32)IGPU_SCRATCH_GTT_OFF;
    Words[5] = 0;
    Words[6] = Want;
    Words[7] = IGPU_MI_NOOP;
    if (!EmitDwords(Words, 7)) {
        LogBcsFault();
        ToyLogBoot("Boot: igpu mem soft (blt to)\n");
        return 0;
    }

    Words[0] = (0x20u << 23) | (1u << 22) | 3u;
    Words[1] = (UINT32)(IGPU_SCRATCH_GTT_OFF + 4u);
    Words[2] = 0;
    Words[3] = IGPU_FENCE_MAGIC;
    if (!EmitDwords(Words, 4)) {
        LogBcsFault();
        ToyLogBoot("Boot: igpu mem soft (fence to)\n");
        return 0;
    }

    Fence = Pix[1];
    Sample = Pix[0];
    ToyLogBoot("Boot: igpu mem fence=");
    ToyLogBootHex32(Fence);
    ToyLogBoot(" blt=");
    ToyLogBootHex32(Sample);
    ToyLogBoot("\n");
    if (Fence != IGPU_FENCE_MAGIC) {
        LogBcsFault();
        ToyLogBoot("Boot: igpu mem soft (fence miss)\n");
        return 0;
    }
    if (Sample != Want) {
        LogBcsFault();
        ToyLogBoot("Boot: igpu mem soft (blt miss)\n");
        return 0;
    }
    if (Pix[IGPU_MEM_W - 1u] != Want
        || Pix[(IGPU_MEM_H - 1u) * (IGPU_MEM_PITCH / 4u)] != Want) {
        ToyLogBoot("Boot: igpu mem soft (corner)\n");
        return 0;
    }
    ToyLogBoot("Boot: igpu blit mem ok\n");
    return 1;
}

/* 读 scanout（GOP LFB / aperture），不读后缓冲 */
static UINT32 ReadFrontPixel(UINT32 X, UINT32 Y, UINT32 PitchPx) {
    UINT64 Base;
    volatile UINT32 *P;

    Base = HalVideoFrameBufferBase();
    if (Base == 0 || PitchPx == 0) {
        return 0;
    }
    P = (volatile UINT32 *)(UINTN)Base;
    /* WC：读前夹 flush 邻近行，减少脏 cache 干扰 */
    FlushCpu((const void *)(UINTN)(Base + ((UINT64)Y * PitchPx + X) * 4ull), 64);
    return P[Y * PitchPx + X];
}

int IgpuBlitColorTest(void) {
    const BOOT_INFO *Info;
    UINT32 PitchB;
    UINT32 PitchPx;
    UINT32 X1;
    UINT32 Y1;
    UINT32 X2;
    UINT32 Y2;
    UINT32 Words[7];
    UINT32 Pixel;
    UINT32 Want;

    if (!gIgpuRingOk) {
        ToyLogBoot("Boot: igpu color soft (no ring)\n");
        return 0;
    }
    if (!IgpuForcewakeGet()) {
        ToyLogBoot("Boot: igpu color soft (fw)\n");
        return 0;
    }

    Info = BootInfoGet();
    if (!Info || Info->PixelsPerScanLine == 0) {
        ToyLogBoot("Boot: igpu color soft (pitch)\n");
        return 0;
    }
    PitchPx = Info->PixelsPerScanLine;
    PitchB = PitchPx * 4u;
    /* 右上角：避开左侧桌面图标列 */
    X2 = Info->HorizontalResolution > 24u ? Info->HorizontalResolution - 24u : IGPU_TEST_W;
    Y1 = 24u;
    X1 = (X2 > IGPU_TEST_W) ? (X2 - IGPU_TEST_W) : 0;
    Y2 = Y1 + IGPU_TEST_H;
    if (X2 > Info->HorizontalResolution || Y2 > Info->VerticalResolution) {
        ToyLogBoot("Boot: igpu color soft (clip)\n");
        return 0;
    }

    Words[0] = IGPU_XY_COLOR_NOLEN | IGPU_BLT_WRITE_RGB | IGPU_BLT_WRITE_ALPHA | 5u;
    Words[1] = IGPU_ROP_COLOR_COPY | IGPU_BR13_DEPTH_32 | (PitchB & 0xFFFFu);
    Words[2] = (Y1 << 16) | (X1 & 0xFFFFu);
    Words[3] = (Y2 << 16) | (X2 & 0xFFFFu);
    Words[4] = (UINT32)IgpuScanoutShownGtt();
    Words[5] = (UINT32)(IgpuScanoutShownGtt() >> 32);
    Words[6] = IGPU_TEST_COLOR;

    if (!EmitDwords(Words, 7)) {
        ToyLogBoot("Boot: igpu color soft (to) head=");
        ToyLogBootHex32(IgpuMmioRead32(IGPU_RING_HEAD));
        ToyLogBoot(" tail=");
        ToyLogBootHex32(IgpuMmioRead32(IGPU_RING_TAIL));
        ToyLogBoot("\n");
        return 0;
    }

    IgpuStallUs(500); /* 给 display 扫完 */
    Pixel = ReadFrontPixel(X1 + 8u, Y1 + 8u, PitchPx);
    Want = IGPU_TEST_COLOR & 0x00FFFFFFu;
    ToyLogBoot("Boot: igpu color at ");
    ToyLogBootHex32(X1);
    ToyLogBoot(",");
    ToyLogBootHex32(Y1);
    ToyLogBoot(" pix=");
    ToyLogBootHex32(Pixel);
    ToyLogBoot("\n");
    if ((Pixel & 0x00FFFFFFu) != Want) {
        ToyLogBoot("Boot: igpu color soft (want=");
        ToyLogBootHex32(Want);
        ToyLogBoot(")\n");
        return 0;
    }
    gIgpuBlitOk = 1;
    ToyLogBoot("Boot: igpu blit color ok\n");
    return 1;
}
