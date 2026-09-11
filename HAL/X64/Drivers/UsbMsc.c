/*
 * UsbMsc.c — BOT 空壳（PR-H-msc-1）
 *
 * 不 include XHCI、不扫口、不认盘。后续 PR 再接 Bulk。
 */
#include "UsbMsc.h"

int UsbMscInit(void) {
    return -1;
}

int UsbMscReady(void) {
    return 0;
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
