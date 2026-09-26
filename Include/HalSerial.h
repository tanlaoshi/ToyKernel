/*
 * HalSerial.h — 串口 HAL 门面（Common 经 Hal.h 使用，不直接 include 驱动头）
 *
 * 串口 TX 受 ToySerialConfig（TOY_SERIAL / 分模块）约束；
 * GOP ring / BootMark 上屏与「有无 COM」策略见 HalSerial.c。
 */
#ifndef HAL_SERIAL_H
#define HAL_SERIAL_H

#include "BootTypes.h"

void HalSerialInitialize(void);
/* COM1 首探失败时再探（与 USB-UART Early 同点） */
void HalSerialRetryIfMissing(void);
int HalSerialPresent(void);
/* video 就绪后：允许 boot 期把 ring 刷到 GOP（与 COM1 无关） */
void HalSerialGopEnable(void);
/*
 * boot 上滚字号：按分辨率挑内建 Terminus（4K→x2，使一屏行数少、可测上滚）。
 * ThemeInitialize / FontInitialize 后若仍在镜像期须再调，避免缩回 10x18。
 */
void HalSerialBootFontApply(void);
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
 * boot GOP 镜像开关。进调度前关：之后有 COM1 只旁路写串口，
 * 无 COM1 只写 ring——主路径不得因串口有无而分叉。
 */
void HalSerialGopMirror(int Enable);
/* 1 = boot 期仍在往 GOP 上滚（ThemeLoad 勿中途换字） */
int HalSerialGopMirroring(void);
void HalSerialBootMark(const char *Text);
void HalSerialBootMarkChannel(int Channel, const char *Text);
int HalSerialDataReady(void);
char HalSerialReadChar(void);
void HalSerialFormatHex(char *Buf, UINT64 Value, int Digits);

#endif
