/*
 * Platform.c — Arm64 平台 MMIO 占位
 */
#include "Hal.h"

void HalPlatformMapMmio(void) {
}

UINT64 HalPlatformXhciFallback(void) {
    return 0;
}
