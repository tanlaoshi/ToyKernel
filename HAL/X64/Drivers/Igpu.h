/*
 * Igpu.h — Intel 核显（PR-G-igpu-0 认卡；igpu-1 MMIO 指纹）
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

/* PR-G-igpu-1：VMM 后映 BAR0；只读指纹。可 blit 的 Ready 仍另计 */
int IgpuMmioInit(void);
int IgpuMmioOk(void);
UINT64 IgpuMmioBarPhys(void);
volatile UINT8 *IgpuMmioBase(void);

#endif
