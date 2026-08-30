/*
 * Serial.c — COM1 串口驱动实现
 *
 * 16550 UART 初始化与轮询发送。Console 与调试信息均经此输出到主机。
 */
#include "Serial.h"

#define COM1 0x3F8

/* 向 I/O 端口写一字节 */
static inline void outb(UINT16 Port, UINT8 Value) {
    __asm__ volatile ("outb %0, %1" : : "a"(Value), "Nd"(Port));
}

/* 从 I/O 端口读一字节 */
static inline UINT8 inb(UINT16 Port) {
    UINT8 Value;
    __asm__ volatile ("inb %1, %0" : "=a"(Value) : "Nd"(Port));
    return Value;
}

/* 初始化 COM1：115200 8N1（除数锁存、线路控制、FIFO） */
void SerialInit(void) {
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x80);
    outb(COM1 + 0, 0x01);
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x03);
    outb(COM1 + 2, 0xC7);
    outb(COM1 + 4, 0x0B);
}

/* 等待发送保持寄存器空闲后输出单字符 */
static void SerialPutChar(char C) {
    int Timeout = 100000;
    while (Timeout-- && !(inb(COM1 + 5) & 0x20)) {
    }
    outb(COM1, (UINT8)C);
}

/* 输出以 NUL 结尾的字符串，\n 前自动补 \r */
int SerialDataReady(void) {
    return (inb(COM1 + 5) & 0x01) != 0;
}

char SerialReadChar(void) {
    return (char)inb(COM1);
}

void SerialWrite(const char *Text) {
    while (*Text) {
        if (*Text == '\n') {
            SerialPutChar('\r');
        }
        SerialPutChar(*Text++);
    }
}

/* 将 Value 格式化为 0x 开头的十六进制字符串写入 Buf */
void HexFormat(char *Buf, UINT64 Value, int Digits) {
    Buf[0] = '0';
    Buf[1] = 'x';
    for (int i = 0; i < Digits; i++) {
        int Digit = (int)((Value >> ((Digits - 1 - i) * 4)) & 0xF);
        Buf[2 + i] = (Digit < 10) ? (char)('0' + Digit) : (char)('A' + Digit - 10);
    }
    Buf[2 + Digits] = '\0';
}

/* 串口输出 32 位十六进制 */
void SerialHex32(UINT32 Value) {
    char Buf[12];
    HexFormat(Buf, Value, 8);
    SerialWrite(Buf);
}

/* 串口输出 64 位十六进制 */
void SerialHex64(UINT64 Value) {
    char Buf[20];
    HexFormat(Buf, Value, 16);
    SerialWrite(Buf);
}
