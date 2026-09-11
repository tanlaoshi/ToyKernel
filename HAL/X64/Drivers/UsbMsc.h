/*
 * UsbMsc.h — USB MSC BOT 门面（PR-H-msc）
 *
 * PR-H-msc-1：仅空壳；Init 失败、Ready=0；不碰 xHCI。
 */
#ifndef USB_MSC_H
#define USB_MSC_H

#include "BootTypes.h"

int UsbMscInit(void);
int UsbMscReady(void);
UINT32 UsbMscBlockCount(void);
int UsbMscReadSectors(UINT32 Lba, UINT32 Count, void *Buffer);
int UsbMscWriteSectors(UINT32 Lba, UINT32 Count, const void *Buffer);

#endif
