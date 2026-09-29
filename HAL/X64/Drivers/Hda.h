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

#endif
