/*
 * HalSerial.h — 串口 HAL 门面（Common 经 Hal.h 使用，不直接 include 驱动头）
 */
#ifndef HAL_SERIAL_H
#define HAL_SERIAL_H

#include "BootTypes.h"

void HalSerialInit(void);
int HalSerialPresent(void);
/* PR-H3：video 就绪后调用，把无 COM1 时缓冲的 boot 日志刷到 GOP */
void HalSerialGopEnable(void);
void HalSerialWrite(const char *Text);
int HalSerialDataReady(void);
char HalSerialReadChar(void);
void HalSerialHexFormat(char *Buf, UINT64 Value, int Digits);

#endif
