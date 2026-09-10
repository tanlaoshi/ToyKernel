/*
 * HalSerial.h — 串口 HAL 门面（Common 经 Hal.h 使用，不直接 include 驱动头）
 *
 * 串口 TX 受 ToySerialConfig（TOY_SERIAL / 分模块）约束；
 * GOP ring / BootMark 上屏与「有无 COM」策略见 HalSerial.c。
 */
#ifndef HAL_SERIAL_H
#define HAL_SERIAL_H

#include "BootTypes.h"

void HalSerialInit(void);
int HalSerialPresent(void);
/* video 就绪后：允许 boot 期把 ring 刷到 GOP（与 COM1 无关） */
void HalSerialGopEnable(void);
/* 未标通道：走 MISC（兼容旧调用） */
void HalSerialWrite(const char *Text);
void HalSerialWriteChannel(int Channel, const char *Text);
void HalSerialWriteChannelHex32(int Channel, UINT32 Value);
void HalSerialWriteChannelHex64(int Channel, UINT64 Value);
/* 常驻 ring，供 Desktop 叠画 */
const char *HalSerialLogText(void);
void HalSerialBootLogRewind(void);
void HalSerialGopMute(int Mute);
/*
 * boot GOP 镜像开关。PHOTO/进调度前关：之后有 COM1 只旁路写串口，
 * 无 COM1 只写 ring——主路径不得因串口有无而分叉。
 */
void HalSerialGopMirror(int Enable);
void HalSerialBootMark(const char *Text);
void HalSerialBootMarkChannel(int Channel, const char *Text);
void HalSerialGopPhotoHold(UINT32 Seconds);
int HalSerialDataReady(void);
char HalSerialReadChar(void);
void HalSerialFormatHex(char *Buf, UINT64 Value, int Digits);

#endif
