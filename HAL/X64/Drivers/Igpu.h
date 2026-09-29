/*
 * Igpu.h — Intel 核显（PR-G-igpu-0…3）
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

/* PR-G-igpu-3：forcewake / GSM / BCS ring */
int IgpuForcewakeInit(void);
int IgpuForcewakeGet(void);
int IgpuForcewakeOk(void);
int IgpuGsmInit(void);
int IgpuGsmOk(void);
int IgpuGsmMap(UINT64 GttOff, UINT64 Phys);
UINT64 IgpuGsmPteRead(UINT64 GttOff);
int IgpuBlitInit(void);
int IgpuBlitColorTest(void); /* 桌面就绪后调用：右上角品红块 */
int IgpuBlitOk(void);

#endif
