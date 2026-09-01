/*
 * Serial.h — COM1 串口驱动接口
 *
 * QEMU 下使用 -serial stdio 可将输出重定向到终端。
 * 端口基址 0x3F8，轮询发送，无接收缓冲。
 */
#ifndef SERIAL_H
#define SERIAL_H

#include "BootTypes.h"

void SerialInit(void);
void SerialWrite(const char *Text);
int SerialDataReady(void);
char SerialReadChar(void);
void SerialHexFormat(char *Buf, UINT64 Value, int Digits);
void SerialHex32(UINT32 Value);
void SerialHex64(UINT64 Value);

#endif
