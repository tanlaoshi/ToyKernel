/*
 * Igpu.h — Intel 核显（PR-G-igpu-0：认卡；后续 blit）
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

#endif
