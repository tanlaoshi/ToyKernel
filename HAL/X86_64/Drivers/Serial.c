/*
 * Serial.c — COM1 串口驱动（经 HalIo，无直接 asm）
 */
#include "Serial.h"
#include "Hal.h"

#define COM1 0x3F8

void SerialInit(void) {
    HalIoWrite8(COM1 + 1, 0x00);
    HalIoWrite8(COM1 + 3, 0x80);
    HalIoWrite8(COM1 + 0, 0x01);
    HalIoWrite8(COM1 + 1, 0x00);
    HalIoWrite8(COM1 + 3, 0x03);
    HalIoWrite8(COM1 + 2, 0xC7);
    HalIoWrite8(COM1 + 4, 0x0B);
}

static void SerialPutChar(char C) {
    int Timeout = 100000;
    while (Timeout-- && !(HalIoRead8(COM1 + 5) & 0x20)) {
    }
    HalIoWrite8(COM1, (UINT8)C);
}

int SerialDataReady(void) {
    return (HalIoRead8(COM1 + 5) & 0x01) != 0;
}

char SerialReadChar(void) {
    return (char)HalIoRead8(COM1);
}

void SerialWrite(const char *Text) {
    while (*Text) {
        if (*Text == '\n') {
            SerialPutChar('\r');
        }
        SerialPutChar(*Text++);
    }
}

void SerialHexFormat(char *Buf, UINT64 Value, int Digits) {
    Buf[0] = '0';
    Buf[1] = 'x';
    for (int i = 0; i < Digits; i++) {
        int Digit = (int)((Value >> ((Digits - 1 - i) * 4)) & 0xF);
        Buf[2 + i] = (Digit < 10) ? (char)('0' + Digit) : (char)('A' + Digit - 10);
    }
    Buf[2 + Digits] = '\0';
}

void SerialHex32(UINT32 Value) {
    char Buf[12];
    SerialHexFormat(Buf, Value, 8);
    SerialWrite(Buf);
}

void SerialHex64(UINT64 Value) {
    char Buf[20];
    SerialHexFormat(Buf, Value, 16);
    SerialWrite(Buf);
}
