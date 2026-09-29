/*
 * IgpuBlitPrivate.h — IgpuBlit / IgpuBlitTest 内部交接（PR-S3-igpublit-1）
 */
#ifndef IGPU_BLIT_PRIVATE_H
#define IGPU_BLIT_PRIVATE_H

#include "Igpu.h"
#include "ToySerialLog.h"

#define IGPU_BLT_RING_BASE   0x22000u
#define IGPU_RING_TAIL       (IGPU_BLT_RING_BASE + 0x30u)
#define IGPU_RING_HEAD       (IGPU_BLT_RING_BASE + 0x34u)
#define IGPU_RING_START      (IGPU_BLT_RING_BASE + 0x38u)
#define IGPU_RING_CTL        (IGPU_BLT_RING_BASE + 0x3cu)
#define IGPU_RING_MODE       (IGPU_BLT_RING_BASE + 0x29cu) /* RING_MODE_GEN7 */
#define IGPU_GFX_PPGTT_EN    (1u << 9)
#define IGPU_RING_VALID      1u
#define IGPU_FW_DISABLE(Bits) ((Bits) << 16) /* MMIO 掩码写：清位 */
#define IGPU_RING_GTT_OFF    0x01000000ull
#define IGPU_SCRATCH_GTT_OFF 0x02000000ull /* 显存自检页，与 ring/scanout 分离 */
#define IGPU_BATCH_GTT_OFF   0x03000000ull /* COLOR 等 2D 包走 batch，不直接塞 ring */
#define IGPU_MI_NOOP         0u
#define IGPU_RING_WAIT_US    200000u
/* Gen 环 HEAD/TAIL：至少 QWord 对齐；部分代 HEAD 以 cacheline 汇报 → 提交后对齐到 64B */
#define IGPU_RING_ALIGN      64u

#define IGPU_XY_COLOR_NOLEN  ((2u << 29) | (0x50u << 22))
#define IGPU_XY_SRC_COPY_NOLEN ((2u << 29) | (0x53u << 22))
#define IGPU_BLT_WRITE_RGB   (1u << 20)
#define IGPU_BLT_WRITE_ALPHA (1u << 21)
/* 32bpp 在 BR13（dword1）：bit24|bit25；勿放进 dword0（B6 已否） */
#define IGPU_BR13_DEPTH_32   ((1u << 24) | (1u << 25))
#define IGPU_ROP_COLOR_COPY  (0xF0u << 16) /* COLOR_BLT 用 PAT */
#define IGPU_ROP_SRC_COPY    (0xCCu << 16) /* SRC_COPY */
/* B8 已否：MI_FLUSH_DW|USE_GTT|STOREDW → IPEHR=0x13004006 卡死 BCS，勿再用 */
#define IGPU_FENCE_MAGIC     0xB13B13B1u
#define IGPU_RING_IPEIR      (IGPU_BLT_RING_BASE + 0x64u)
#define IGPU_RING_IPEHR      (IGPU_BLT_RING_BASE + 0x68u)
#define IGPU_RING_EIR        (IGPU_BLT_RING_BASE + 0xb0u)
#define IGPU_TEST_COLOR      0x00FF00FFu /* 品红 */
#define IGPU_TEST_W          160u
#define IGPU_TEST_H          80u
#define IGPU_MEM_PITCH       256u /* 64 px * 4 */
#define IGPU_MEM_W           64u
#define IGPU_MEM_H           16u  /* 64*16*4 = 4096 */

extern int gIgpuBlitOk;
extern int gIgpuRingOk;

void FlushCpu(const void *Ptr, UINTN Size);
int EmitDwords(const UINT32 *Words, UINT32 Count);
int SubmitMemTest(void);

#endif
