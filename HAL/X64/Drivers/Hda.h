/*
 * Hda.h — Intel HD Audio（PR-G-audio）
 */
#ifndef HDA_H
#define HDA_H

#include "BootTypes.h"

void HdaDriverRegister(void);

/* 1=已认到 HDA PCI；尚未表示可播放 */
int HdaProbed(void);
UINT16 HdaPciDid(void);
UINT8 HdaPciBus(void);
UINT8 HdaPciDev(void);
UINT8 HdaPciFn(void);

/* PR-G-audio-1：VMM 后映 BAR0；只读指纹 */
int HdaMmioInit(void);
int HdaMmioOk(void);
UINT64 HdaMmioBarPhys(void);
volatile UINT8 *HdaMmioBase(void);
UINTN HdaMmioMapBytes(void);
UINT32 HdaMmioRead32(UINT32 Off);
UINT16 HdaMmioRead16(UINT32 Off);
UINT8 HdaMmioRead8(UINT32 Off);

#endif
