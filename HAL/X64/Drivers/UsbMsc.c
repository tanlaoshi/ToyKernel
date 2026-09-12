/*
 * UsbMsc.c — BOT 门面（PR-H-msc-2…5）
 *
 * Init：Bulk 环。Scan：class 日志。Claim：SetConfig+Bulk。
 * Capacity：INQUIRY + READ CAPACITY(10)；不读分区、不挂 FAT。
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
    (void)Lba;
    (void)Count;
    (void)Buffer;
    return 0; /* msc-6+ */
}

int UsbMscWriteSectors(UINT32 Lba, UINT32 Count, const void *Buffer) {
    (void)Lba;
    (void)Count;
    (void)Buffer;
    return 0;
}
