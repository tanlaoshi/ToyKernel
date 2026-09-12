/*
 * UsbMsc.h — USB MSC BOT 门面（PR-H-msc）
 *
 * PR-H-msc-2：Init → XhciMscBringUp；Ready=0。
 * PR-H-msc-3：UsbMscScan → 非键鼠口 class 日志后放弃。
 * PR-H-msc-4：UsbMscClaim → 单口 SetConfig + Bulk；不 SCSI。
 * PR-H-msc-5：UsbMscCapacity → INQUIRY + READ CAPACITY(10)。
 */
#ifndef USB_MSC_H
#define USB_MSC_H

#include "BootTypes.h"

int UsbMscInit(void);
int UsbMscReady(void);
int UsbMscScan(void);     /* PR-H-msc-3：返回扫到的口数；失败 -1 */
int UsbMscClaim(void);    /* PR-H-msc-4：1 ok；0 none；-1 no hc */
int UsbMscCapacity(void); /* PR-H-msc-5：0 ok；-1 fail / 未 claim */
UINT32 UsbMscBlockCount(void);
UINT32 UsbMscBlockSize(void);
int UsbMscReadSectors(UINT32 Lba, UINT32 Count, void *Buffer);
int UsbMscWriteSectors(UINT32 Lba, UINT32 Count, const void *Buffer);

#endif
