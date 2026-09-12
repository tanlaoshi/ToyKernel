/*
 * UsbMsc.c — BOT 门面（PR-H-msc-2…6）
 *
 * Init：Bulk 环。Scan：class 日志。Claim：SetConfig+Bulk。
 * Capacity：INQUIRY + READ CAPACITY(10)。
 * Read：BOT READ(10)；写仍失败。挂载经 BlockMux + remount（Shell msc mount）。
 */
#include "UsbMsc.h"
#include "XHCI.h"

int UsbMscInit(void) {
    return XhciMscBringUp();
}

int UsbMscReady(void) {
    return XhciMscReady();
}

int UsbMscScan(void) {
    return XhciMscScanPorts();
}

int UsbMscClaim(void) {
    return XhciMscClaimPorts();
}

int UsbMscCapacity(void) {
    return XhciMscCapacity();
}

UINT32 UsbMscBlockCount(void) {
    return XhciMscBlockCount();
}

UINT32 UsbMscBlockSize(void) {
    return XhciMscBlockSize();
}

int UsbMscReadSectors(UINT32 Lba, UINT32 Count, void *Buffer) {
    return XhciMscReadSectors(Lba, Count, Buffer);
}

int UsbMscWriteSectors(UINT32 Lba, UINT32 Count, const void *Buffer) {
    (void)Lba;
    (void)Count;
    (void)Buffer;
    return 0; /* msc-6：只读挂载 */
}
