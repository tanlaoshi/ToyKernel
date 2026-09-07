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
/* 无 COM1 时的常驻日志缓冲（供 Desktop 叠画；有 COM1 时可能为空） */
const char *HalSerialLogText(void);
/* 清屏日志区并重置行距，避免底行半截/写穿 */
void HalSerialBootLogRewind(void);
void HalSerialGopMute(int Mute);
/* 真机：直写帧缓冲一行进度（不 Present），并入环/串口 */
void HalSerialBootMark(const char *Text);
int HalSerialDataReady(void);
char HalSerialReadChar(void);
void HalSerialFormatHex(char *Buf, UINT64 Value, int Digits);

#endif
