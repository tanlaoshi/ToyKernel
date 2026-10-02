/*
 * Hal/riscv/Io.c — 无传统端口 I/O；占位实现
 */
#include "Hal.h"

UINT8 HalIoRead8(UINT16 Port) {
    (void)Port;
    return 0xFF;
}

UINT16 HalIoRead16(UINT16 Port) {
    (void)Port;
    return 0xFFFF;
}

UINT32 HalIoRead32(UINT16 Port) {
    (void)Port;
    return 0xFFFFFFFF;
}

void HalIoWrite8(UINT16 Port, UINT8 Value) {
    (void)Port;
    (void)Value;
}

void HalIoWrite16(UINT16 Port, UINT16 Value) {
    (void)Port;
    (void)Value;
}

void HalIoWrite32(UINT16 Port, UINT32 Value) {
    (void)Port;
    (void)Value;
}
