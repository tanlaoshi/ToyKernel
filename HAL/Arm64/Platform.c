/*
 * Platform.c — Arm64 平台 MMIO 占位
 */
#include "Hal.h"

void HalPlatformMapMmio(void) {
}

UINT64 HalPlatformXhciFallback(void) {
    return 0;
}

void HalPlatformSetSystemTable(void *SystemTable) {
    (void)SystemTable;
}

void *HalPlatformSystemTable(void) {
    return 0;
}

void HalPlatformNoteRuntimeRange(UINT64 Phys, UINT64 Size) {
    (void)Phys;
    (void)Size;
}

int HalPlatformRuntimeRangeCount(void) {
    return 0;
}

int HalRtcGetTime(UINT16 *Year, UINT8 *Month, UINT8 *Day,
                  UINT8 *Hour, UINT8 *Minute, UINT8 *Second) {
    (void)Year;
    (void)Month;
    (void)Day;
    (void)Hour;
    (void)Minute;
    (void)Second;
    return -1;
}
