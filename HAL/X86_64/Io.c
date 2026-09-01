/*
 * Hal/x86_64/Io.c — x86 端口 I/O（in/out）
 */
#include "Hal.h"

UINT8 HalIoRead8(UINT16 Port) {
    UINT8 Value;
    __asm__ volatile ("inb %1, %0" : "=a"(Value) : "Nd"(Port));
    return Value;
}

UINT16 HalIoRead16(UINT16 Port) {
    UINT16 Value;
    __asm__ volatile ("inw %1, %0" : "=a"(Value) : "Nd"(Port));
    return Value;
}

UINT32 HalIoRead32(UINT16 Port) {
    UINT32 Value;
    __asm__ volatile ("inl %1, %0" : "=a"(Value) : "Nd"(Port));
    return Value;
}

void HalIoWrite8(UINT16 Port, UINT8 Value) {
    __asm__ volatile ("outb %0, %1" : : "a"(Value), "Nd"(Port));
}

void HalIoWrite16(UINT16 Port, UINT16 Value) {
    __asm__ volatile ("outw %0, %1" : : "a"(Value), "Nd"(Port));
}

void HalIoWrite32(UINT16 Port, UINT32 Value) {
    __asm__ volatile ("outl %0, %1" : : "a"(Value), "Nd"(Port));
}
