/*
 * Serial.h — COM1 串口驱动（仅 HAL 内部使用，Common 请用 HalSerial.h）
 */
#ifndef SERIAL_H
#define SERIAL_H

#include "BootTypes.h"

void SerialInitialize(void);
int SerialPresent(void);
void SerialWrite(const char *Text);
/*
 * PR-K-rm-exc-1：首个异常核独占 UART。其它核再调 SerialWrite 直接丢弃，
 * 避免 EXCEPTION 与 store: removed 多核字符交织。
 * 调用方须已 ArchCli；非本核若已有 owner 则本函数永不返回（cli+hlt）。
 */
void SerialClaimException(void);
int SerialDataReady(void);
char SerialReadChar(void);
void SerialHexFormat(char *Buf, UINT64 Value, int Digits);
void SerialHex32(UINT32 Value);
void SerialHex64(UINT64 Value);

#endif
