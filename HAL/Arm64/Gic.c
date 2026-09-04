/*
 * Gic.c — PR-A13：QEMU virt GICv2（distributor + CPU interface）
 * 硬编码 virt 布局；PPI 27 = CNTV。
 * 用 MMIO GICC，避免部分工具链/CPU 上 ICC 系统寄存器未实现。
 */
#include "Hal.h"

#define GICD_BASE       0x08000000ULL
#define GICC_BASE       0x08010000ULL

#define GICD_CTLR       0x000
#define GICD_ISENABLER  0x100
#define GICD_IPRIORITYR 0x400
#define GICD_ITARGETSR  0x800
#define GICD_ICFGR      0xC00

#define GICC_CTLR       0x000
#define GICC_PMR        0x004
#define GICC_IAR        0x00C
#define GICC_EOIR       0x010

#define GIC_TIMER_PPI   27u

static volatile UINT32 *Reg32(UINT64 Base, UINT32 Off) {
    return (volatile UINT32 *)(UINTN)(Base + Off);
}

static void W32(UINT64 Base, UINT32 Off, UINT32 Val) {
    *Reg32(Base, Off) = Val;
}

static UINT32 R32(UINT64 Base, UINT32 Off) {
    return *Reg32(Base, Off);
}

static void W8(UINT64 Base, UINT32 Off, UINT8 Val) {
    *(volatile UINT8 *)(UINTN)(Base + Off) = Val;
}

void HalGicInit(void) {
    /* Distributor：开 PPI 27，目标 CPU0，优先级中等 */
    W8(GICD_BASE, GICD_IPRIORITYR + GIC_TIMER_PPI, 0x80);
    W8(GICD_BASE, GICD_ITARGETSR + GIC_TIMER_PPI, 0x01);
    W32(GICD_BASE, GICD_ISENABLER + 0, 1u << GIC_TIMER_PPI);
    W32(GICD_BASE, GICD_CTLR, 1);

    /* CPU interface */
    W32(GICC_BASE, GICC_PMR, 0xF0);
    W32(GICC_BASE, GICC_CTLR, 1);
}

UINT32 HalGicAck(void) {
    return R32(GICC_BASE, GICC_IAR) & 0x3FFu;
}

void HalGicEoi(UINT32 IntId) {
    if (IntId < 1020u) {
        W32(GICC_BASE, GICC_EOIR, IntId);
    }
}

int HalGicIsTimer(UINT32 IntId) {
    return IntId == GIC_TIMER_PPI;
}
