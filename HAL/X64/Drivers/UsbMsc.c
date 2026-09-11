/*
 * UsbMsc.c — BOT 门面（PR-H-msc-2）
 *
 * 接 xHCI Bulk API 壳；BringUp 恒失败；不扫口、不认盘。
 */
#include "UsbMsc.h"
#include "XHCI.h"

int UsbMscInit(void) {
    /* 仅调 XhciMscBringUp：InitRing Bulk 环后恒 -1 */
    return XhciMscBringUp();
}

int UsbMscReady(void) {
    return XhciMscReady();
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
