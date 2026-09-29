/*
 * ArchPrivate.h — x86 Arch.c / ArchInterrupt.c 内部交接（PR-S3-arch-1）
 */
#ifndef ARCH_PRIVATE_H
#define ARCH_PRIVATE_H

#include "HalPort.h"

void ArchIdtLoad(void);
void ArchIdtLidt(void);
void ArchPicMaskAll(void);
void ArchLapicEnable(void);
void ArchLapicSelfIpi(UINT8 Vector);

extern volatile UINT32 gArchIrqCount;

#endif
