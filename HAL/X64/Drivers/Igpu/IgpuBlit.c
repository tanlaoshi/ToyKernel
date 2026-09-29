/*
 * IgpuBlit.c — BCS ring + MI_NOOP（PR-S3-igpublit-1）
 *
 * mem/色块自检见 IgpuBlitTest.c。
 */
#include "Igpu.h"
#include "PhysicalMemory.h"
#include "IgpuBlitPrivate.h"

int gIgpuBlitOk;
int gIgpuRingOk;
static UINT32 *gRing;
static UINT64 gRingPhys;
static UINT32 gRingTail;

int IgpuBlitOk(void) {
    return gIgpuBlitOk;
}

/* igpu-4：Present/CopyRect 共用 BCS 提交 */
int IgpuBlitEmit(const UINT32 *Words, UINT32 Count) {
    if (!gIgpuRingOk || !Words || Count == 0) {
        return 0;
    }
    if (!IgpuForcewakeGet()) {
        return 0;
    }
    return EmitDwords(Words, Count);
}

void FlushCpu(const void *Ptr, UINTN Size) {
    const UINT8 *P = (const UINT8 *)Ptr;
    UINTN Off;

    for (Off = 0; Off < Size; Off += 64) {
        __asm__ volatile ("clflush (%0)" : : "r"(P + Off) : "memory");
    }
    __asm__ volatile ("mfence" ::: "memory");
}

static int WaitHead(UINT32 WantTail) {
    UINT32 Waited;
    UINT32 Want;

    /* WantTail 须已按 IGPU_RING_ALIGN 对齐（EmitDwords 保证） */
    Want = WantTail & ~(IGPU_RING_ALIGN - 1u);
    for (Waited = 0; Waited < IGPU_RING_WAIT_US; Waited += 20u) {
        UINT32 Head = IgpuMmioRead32(IGPU_RING_HEAD) & ~(IGPU_RING_ALIGN - 1u);

        if (Head == Want) {
            return 1;
        }
        IgpuStallUs(20);
    }
    return 0;
}

static int RingRestart(void) {
    UINT32 Waited;

    IgpuMmioWrite32(IGPU_RING_CTL, 0);
    for (Waited = 0; Waited < 50000u; Waited += 20u) {
        if ((IgpuMmioRead32(IGPU_RING_CTL) & IGPU_RING_VALID) == 0) {
            break;
        }
        IgpuStallUs(20);
    }
    IgpuMmioWrite32(IGPU_RING_HEAD, 0);
    IgpuMmioWrite32(IGPU_RING_TAIL, 0);
    IgpuMmioWrite32(IGPU_RING_START, (UINT32)IGPU_RING_GTT_OFF);
    IgpuMmioWrite32(IGPU_RING_CTL, IGPU_RING_VALID);
    gRingTail = 0;
    IgpuStallUs(50);
    return 1;
}

static int ArmRing(void) {
    void *Page;
    UINT32 i;

    Page = PhysicalMemoryAllocatePage();
    if (!Page) {
        ToyLogBoot("Boot: igpu blit soft (oom)\n");
        return 0;
    }
    gRing = (UINT32 *)Page;
    gRingPhys = (UINT64)(UINTN)Page;
    for (i = 0; i < (PAGE_SIZE / 4u); i++) {
        gRing[i] = IGPU_MI_NOOP;
    }
    FlushCpu(gRing, PAGE_SIZE);
    if (!IgpuGsmMap(IGPU_RING_GTT_OFF, gRingPhys)) {
        ToyLogBoot("Boot: igpu blit soft (pte)\n");
        return 0;
    }
    return RingRestart();
}

int EmitDwords(const UINT32 *Words, UINT32 Count) {
    UINT32 i;
    UINT32 Off;
    UINT32 NewTail;
    UINT32 PadTail;
    UINT32 Need;

    Off = gRingTail / 4u;
    /* 对齐到 cacheline 后的终点 */
    Need = (Count * 4u + (IGPU_RING_ALIGN - 1u)) & ~(IGPU_RING_ALIGN - 1u);
    if ((Off * 4u) + Need >= PAGE_SIZE) {
        if (!WaitHead(gRingTail)) {
            return 0;
        }
        if (!RingRestart()) {
            return 0;
        }
        Off = 0;
    }
    for (i = 0; i < Count; i++) {
        gRing[Off + i] = Words[i];
    }
    NewTail = (Off + Count) * 4u;
    PadTail = (NewTail + (IGPU_RING_ALIGN - 1u)) & ~(IGPU_RING_ALIGN - 1u);
    while (NewTail < PadTail) {
        gRing[NewTail / 4u] = IGPU_MI_NOOP;
        NewTail += 4u;
    }
    FlushCpu(&gRing[Off], (NewTail - Off * 4u));
    IgpuMmioWrite32(IGPU_RING_TAIL, NewTail);
    if (!WaitHead(NewTail)) {
        return 0;
    }
    gRingTail = NewTail;
    return 1;
}

static int SubmitNoops(void) {
    UINT32 Words[8];
    UINT32 i;

    for (i = 0; i < 8; i++) {
        Words[i] = IGPU_MI_NOOP;
    }
    if (!EmitDwords(Words, 8)) {
        ToyLogBoot("Boot: igpu blit soft (head to)\n");
        return 0;
    }
    return 1;
}

int IgpuBlitInit(void) {
    if (gIgpuRingOk) {
        return 1;
    }
    if (!IgpuGttOk()) {
        ToyLogBoot("Boot: igpu blit soft (need surf)\n");
        return 0;
    }
    if (!IgpuForcewakeOk()) {
        ToyLogBoot("Boot: igpu blit soft (need fw)\n");
        return 0;
    }
    if (!IgpuGsmInit()) {
        return 0;
    }
    if (!ArmRing()) {
        return 0;
    }
    if (!SubmitNoops()) {
        return 0;
    }
    ToyLogBoot("Boot: igpu blit ring ok\n");
    if (!SubmitMemTest()) {
        return 0;
    }
    gIgpuRingOk = 1;
    gIgpuBlitOk = 1; /* mem 回读过即算 blit 通路可用；屏上色块仅演示 */
    return 1;
}
