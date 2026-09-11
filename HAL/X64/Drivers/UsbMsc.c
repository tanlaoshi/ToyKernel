/*
 * UsbMsc.c — BOT 门面（PR-H-msc-2/3）
 *
 * Init：BringUp 壳（恒失败）。Scan：委托 XhciMscScanPorts。
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

UINT32 UsbMscBlockCount(void) {
    return 0;
}

int UsbMscReadSectors(UINT32 Lba, UINT32 Count, void *Buffer) {
    (void)Lba;
    (void)Count;
    (void)Buffer;
    return 0;
}

int UsbMscWriteSectors(UINT32 Lba, UINT32 Count, const void *Buffer) {
    (void)Lba;
    (void)Count;
    (void)Buffer;
    return 0;
}
