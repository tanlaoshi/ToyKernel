/*
 * Serial.h — COM1 串口驱动（仅 HAL 内部使用，Common 请用 HalSerial.h）
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
