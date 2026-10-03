/*
 * Igpu.h — Intel 核显（PR-G-igpu-0…4）
 */
#ifndef IGPU_H
#define IGPU_H

#include "BootTypes.h"

void IgpuDriverRegister(void);

/* 1=已认到 Intel display 类 PCI；尚未表示可 blit */
int IgpuProbed(void);
UINT16 IgpuPciDid(void);
UINT8 IgpuPciBus(void);
UINT8 IgpuPciDev(void);
UINT8 IgpuPciFn(void);

/* PR-G-igpu-1：VMM 后映 BAR0 */
int IgpuMmioInit(void);
int IgpuMmioOk(void);
UINT64 IgpuMmioBarPhys(void);
volatile UINT8 *IgpuMmioBase(void);
UINTN IgpuMmioMapBytes(void);
UINT32 IgpuMmioRead32(UINT32 Off);
void IgpuMmioWrite32(UINT32 Off, UINT32 Val);
void IgpuStallUs(UINT32 Us);

/* PR-G-igpu-2：观察固件 scanout（不写 PTE） */
int IgpuGttInit(void);
int IgpuGttOk(void);
UINT32 IgpuGttSurf(void);
UINT32 IgpuGttSurfReg(void); /* PLANE_SURF_* MMIO 偏移 */

/* PR-G-igpu-3：forcewake / GSM / BCS ring */
int IgpuForcewakeInit(void);
int IgpuForcewakeGet(void);
int IgpuForcewakeOk(void);
int IgpuGsmInit(void);
int IgpuGsmOk(void);
int IgpuGsmMap(UINT64 GttOff, UINT64 Phys);
int IgpuGsmMapQuiet(UINT64 GttOff, UINT64 Phys); /* 批量映；末尾 IgpuGsmFlush */
void IgpuGsmFlush(void);
UINT64 IgpuGsmPteRead(UINT64 GttOff);
int IgpuBlitInit(void);
int IgpuBlitColorTest(void); /* 桌面就绪后调用：右上角品红块 */
int IgpuBlitOk(void);
int IgpuBlitEmit(const UINT32 *Words, UINT32 Count);

/* PR-G-igpu-4：Present / CopyRect 挂钩 */
int IgpuReady(void);
UINT32 IgpuBlitMinPixels(void);
void IgpuNotePresentSkipScale(void);
void IgpuPresentInvalidate(void);
void IgpuPresentPrepare(void);
int IgpuPresentCopyOk(void); /* 1=屏外 SRC_COPY 探针通过（不改 scanout） */
void IgpuBackInvalidate(void);
UINT64 IgpuBackGttOff(void);
UINT64 IgpuFrontGttBase(void);
int IgpuFrontMapEnsure(UINT64 PhysBase, UINTN Bytes);
int IgpuBackMap(UINT64 PhysBase, UINTN Bytes);
int IgpuSrcCopyRect(UINT32 SrcX, UINT32 SrcY, UINT32 DstX, UINT32 DstY,
                    UINT32 W, UINT32 H, UINT32 SrcPitchB, UINT32 DstPitchB,
                    UINT64 SrcGtt, UINT64 DstGtt);
int IgpuPresentRect(const UINT32 *Back, UINT32 BackPitchPx, UINT32 FrontPitchPx,
                    UINT32 BackH, UINT32 X0, UINT32 Y0, UINT32 X1, UINT32 Y1);
int IgpuCopyRectBack(const UINT32 *Back, UINT32 PitchPx, UINT32 BufH,
                     UINT32 SrcX, UINT32 SrcY, UINT32 DstX, UINT32 DstY,
                     UINT32 W, UINT32 H);

/* igpu-5：双缓冲翻页 */
int IgpuScanoutOk(void);
int IgpuScanoutInit(void);
void IgpuScanoutInvalidate(void);
int IgpuScanoutPresent(UINT64 BackGtt, UINT32 BackPitchPx);
UINT64 IgpuScanoutShownGtt(void);

#endif
