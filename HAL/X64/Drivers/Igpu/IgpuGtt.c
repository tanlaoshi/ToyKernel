/*
 * IgpuGtt.c — PR-G-igpu-2：观察固件留下的 scanout（不写 GGTT PTE）
 *
 * Gen9 上 UEFI GOP 已把帧缓冲挂进显示引擎；读 PLANE_SURF 确认 GPU 可见地址，
 * 供后续 blit 复用。写 PTE 风险高，本刀禁止。
 */
#include "Igpu.h"
#include "HalVideo.h"
#include "ToySerialLog.h"

/* SKL/KBL PIPE_A primary plane SURF（i915 PLANE_SURF / 旧 DSPSURF） */
#define IGPU_REG_PLANE_SURF_A  0x7019Cu
#define IGPU_REG_PLANE_SURF_B  0x7119Cu
#define IGPU_REG_PLANE_SURF_C  0x7219Cu

static int gIgpuGttOk;
static UINT32 gIgpuSurf;
static UINT64 gIgpuFbPhys;

int IgpuGttOk(void) {
    return gIgpuGttOk;
}

UINT32 IgpuGttSurf(void) {
    return gIgpuSurf;
}

static UINT32 PickSurf(void) {
    UINT32 A;
    UINT32 B;
    UINT32 C;

    A = IgpuMmioRead32(IGPU_REG_PLANE_SURF_A);
    B = IgpuMmioRead32(IGPU_REG_PLANE_SURF_B);
    C = IgpuMmioRead32(IGPU_REG_PLANE_SURF_C);
    /* 优先非空、非全 F 的 SURF（4K 对齐） */
    if (A != 0 && A != 0xFFFFFFFFu && (A & 0xFFFu) == 0) {
        return A;
    }
    if (B != 0 && B != 0xFFFFFFFFu && (B & 0xFFFu) == 0) {
        return B;
    }
    if (C != 0 && C != 0xFFFFFFFFu && (C & 0xFFFu) == 0) {
        return C;
    }
    return 0;
}

int IgpuGttInit(void) {
    UINT32 Surf;
    UINT64 Fb;

    if (gIgpuGttOk) {
        return 1;
    }
    if (!IgpuMmioOk()) {
        return 0;
    }

    Fb = HalVideoFrameBufferBase();
    gIgpuFbPhys = Fb;
    Surf = PickSurf();
    gIgpuSurf = Surf;

    if (Surf == 0) {
        /*
         * 无 forcewake 时部分机读 SURF=0；不写 PTE、不挡桌面。
         * blit 刀再考虑 forcewake / 显式挂 FB。
         */
        ToyLogBoot("Boot: igpu gtt surf=0 (observe-only, soft)\n");
        ToyLogBoot("Boot: igpu gtt fb=");
        ToyLogBootHex32((UINT32)Fb);
        if (Fb > 0xFFFFFFFFu) {
            ToyLogBoot(":");
            ToyLogBootHex32((UINT32)(Fb >> 32));
        }
        ToyLogBoot("\n");
        return 0;
    }

    gIgpuGttOk = 1;
    ToyLogBoot("Boot: igpu gtt ok surf=");
    ToyLogBootHex32(Surf);
    ToyLogBoot(" fb=");
    ToyLogBootHex32((UINT32)Fb);
    if (Fb > 0xFFFFFFFFu) {
        ToyLogBoot(":");
        ToyLogBootHex32((UINT32)(Fb >> 32));
    }
    ToyLogBoot(" (reuse firmware)\n");
    return 1;
}
