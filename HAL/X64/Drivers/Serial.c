/*
 * Serial.c — COM1 串口驱动（经 HalIo；PR-H3：探测存在性）
 *
 * TOY_SERIAL=0：不 Probe、不碰端口，一切 TX/RX 空操作。
 */
#include "Serial.h"
#include "Hal.h"
#include "ToySerialConfig.h"

#define COM1 0x3F8

static int gSerialOk;

static int ProbeCom1(void) {
    UINT8 A;
    UINT8 B;

#if !TOY_SERIAL
    return 0;
#endif
    /* Scratch 寄存器（offset 7）：无 16550 时常读回 0xFF */
    HalIoWrite8(COM1 + 7, 0x55);
    A = HalIoRead8(COM1 + 7);
    HalIoWrite8(COM1 + 7, 0xAA);
    B = HalIoRead8(COM1 + 7);
    return (A == 0x55 && B == 0xAA) ? 1 : 0;
}

void SerialInit(void) {
#if !TOY_SERIAL
    gSerialOk = 0;
    return;
#else
    gSerialOk = ProbeCom1();
    if (!gSerialOk) {
        return;
    }
    HalIoWrite8(COM1 + 1, 0x00);
    HalIoWrite8(COM1 + 3, 0x80);
    HalIoWrite8(COM1 + 0, 0x01);
    HalIoWrite8(COM1 + 1, 0x00);
    HalIoWrite8(COM1 + 3, 0x03);
    HalIoWrite8(COM1 + 2, 0xC7);
    HalIoWrite8(COM1 + 4, 0x0B);
#endif
}

int SerialPresent(void) {
#if !TOY_SERIAL
    return 0;
#else
    return gSerialOk;
#endif
}

static void SerialPutChar(char C) {
    int Timeout;

    if (!gSerialOk) {
        return;
    }
    /* 短等即可：真机旁路日志，勿空转拖死 BSP（对端未读/无线时） */
    Timeout = 2000;
    while (Timeout-- && !(HalIoRead8(COM1 + 5) & 0x20)) {
        __asm__ volatile ("pause");
    }
    HalIoWrite8(COM1, (UINT8)C);
}

int SerialDataReady(void) {
    if (!gSerialOk) {
        return 0;
    }
    return (HalIoRead8(COM1 + 5) & 0x01) != 0;
}

char SerialReadChar(void) {
    if (!gSerialOk) {
        return 0;
    }
    return (char)HalIoRead8(COM1);
}

void SerialWrite(const char *Text) {
    if (!gSerialOk || !Text) {
        return;
    }
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
