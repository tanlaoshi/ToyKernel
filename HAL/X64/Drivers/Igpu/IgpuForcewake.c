/*
 * IgpuForcewake.c — PR-G-igpu-3：Gen9 Render/GT forcewake（对照 i915）
 *
 * ACK：Render=0x0D84；GT/Blitter=0x130044（勿用 Media 的 0x0D88）。
 * 先等 ACK 清再置位；失败软退，不挡桌面。持有 wake 不 Put（bringup）。
 */
#include "Igpu.h"
#include "ToySerialLog.h"

#define IGPU_FW_KERNEL           1u
#define IGPU_FW_FALLBACK         (1u << 15)
#define IGPU_FW_ENABLE(Bits)     (((Bits) << 16) | (Bits))
#define IGPU_FW_DISABLE(Bits)    ((Bits) << 16)
#define IGPU_REG_FW_RENDER       0xa278u
#define IGPU_REG_FW_ACK_RENDER   0x0D84u
#define IGPU_REG_FW_GT           0xa188u
#define IGPU_REG_FW_ACK_GT       0x130044u /* i915 FORCEWAKE_ACK_GT_GEN9 */
#define IGPU_REG_TIMESTAMP       0x2358u
#define IGPU_FW_ACK_TIMEOUT_US   50000u

static int gIgpuFwOk;
static int gIgpuFwHeld;
static int gIgpuFwTried; /* 1=已试过；失败也不再刷 ack to */

int IgpuForcewakeOk(void) {
    return gIgpuFwOk;
}

static int WaitAckEq(UINT32 AckReg, UINT32 Bit, UINT32 WantSet) {
    UINT32 Waited;

    for (Waited = 0; Waited < IGPU_FW_ACK_TIMEOUT_US; Waited += 10u) {
        UINT32 V = IgpuMmioRead32(AckReg) & Bit;
        if (WantSet ? (V != 0) : (V == 0)) {
            return 1;
        }
        IgpuStallUs(10);
    }
    return 0;
}

static int DomainGet(UINT32 FwReg, UINT32 AckReg, const char *Name) {
    /* 已醒则无需再抢 */
    if ((IgpuMmioRead32(AckReg) & IGPU_FW_KERNEL) != 0) {
        return 1;
    }
    /* i915：先等旧 ACK 清（未醒时应为 0） */
    if (!WaitAckEq(AckReg, IGPU_FW_KERNEL, 0)) {
        ToyLogBoot("Boot: igpu fw ");
        ToyLogBoot(Name);
        ToyLogBoot(" clr to\n");
        return 0;
    }
    IgpuMmioWrite32(FwReg, IGPU_FW_ENABLE(IGPU_FW_KERNEL));
    if (WaitAckEq(AckReg, IGPU_FW_KERNEL, 1)) {
        return 1;
    }
    /* 回退位（i915 FORCEWAKE_KERNEL_FALLBACK） */
    IgpuMmioWrite32(FwReg, IGPU_FW_ENABLE(IGPU_FW_FALLBACK));
    IgpuStallUs(50);
    if (WaitAckEq(AckReg, IGPU_FW_KERNEL, 1)) {
        IgpuMmioWrite32(FwReg, IGPU_FW_DISABLE(IGPU_FW_FALLBACK));
        return 1;
    }
    IgpuMmioWrite32(FwReg, IGPU_FW_DISABLE(IGPU_FW_FALLBACK));
    ToyLogBoot("Boot: igpu fw ");
    ToyLogBoot(Name);
    ToyLogBoot(" ack to\n");
    return 0;
}

int IgpuForcewakeGet(void) {
    if (gIgpuFwHeld) {
        return 1;
    }
    if (gIgpuFwTried && !gIgpuFwOk) {
        return 0; /* 已失败：别再打黄字 */
    }
    if (!IgpuMmioOk()) {
        return 0;
    }
    gIgpuFwTried = 1;
    if (!DomainGet(IGPU_REG_FW_GT, IGPU_REG_FW_ACK_GT, "gt")) {
        return 0;
    }
    if (!DomainGet(IGPU_REG_FW_RENDER, IGPU_REG_FW_ACK_RENDER, "render")) {
        return 0;
    }
    gIgpuFwHeld = 1;
    gIgpuFwOk = 1;
    return 1;
}

int IgpuForcewakeInit(void) {
    UINT32 Ts;

    if (gIgpuFwOk) {
        return 1;
    }
    if (!IgpuMmioOk()) {
        return 0;
    }
    if (!IgpuForcewakeGet()) {
        ToyLogBoot("Boot: igpu fw soft (ack)\n");
        return 0;
    }

    Ts = IgpuMmioRead32(IGPU_REG_TIMESTAMP);
    ToyLogBoot("Boot: igpu fw ok ts=");
    ToyLogBootHex32(Ts);
    ToyLogBoot("\n");
    return 1;
}
