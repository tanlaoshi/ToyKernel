/*
 * UsbMsc.h — USB MSC BOT 门面（PR-H-msc）
 *
 * PR-H-msc-2…5：Init / Scan / Claim / Capacity。
 * PR-H-msc-6：UsbMscMount → BlockMux。
 * PR-H-msc-7b：UsbMscAutoBeforeFs（Live 默认开；msc=0 可关）。
 */
#ifndef USB_MSC_H
#define USB_MSC_H

#include "BootTypes.h"

int UsbMscInit(void);
int UsbMscReady(void);
int UsbMscScan(void);
int UsbMscClaim(void);
int UsbMscCapacity(void);
UINT32 UsbMscBlockCount(void);
UINT32 UsbMscBlockSize(void);
int UsbMscReadSectors(UINT32 Lba, UINT32 Count, void *Buffer);
int UsbMscWriteSectors(UINT32 Lba, UINT32 Count, const void *Buffer);
int UsbMscFlush(void);
int UsbMscMount(void);

/* PR-H-msc-7b：Live 默认 1；UsbMscAutoSet(0) / 卷上 MSC.OFF|THEME msc=0 */
int UsbMscAutoEnabled(void);
void UsbMscAutoSet(int On);
int UsbMscAutoBeforeFs(void);

#endif
