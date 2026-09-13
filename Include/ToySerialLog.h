/*
 * ToySerialLog.h — 分模块串口日志宏
 *
 * ToyLog*     ：始终进 ring（PHOTO）；UART 由通道开关；屏上仅 BOOT 通道上滚
 * ToyBootMark*：BootMark（BOOT/USB 里程碑上屏上滚）；UART 同样受通道约束
 *
 * 总开关 TOY_SERIAL=0：ChannelUartOn 全关 → 无 UART；ring / 屏上 boot 仍可用
 * 分模块 SERIAL_*=0：该通道 quiet（无 UART），其它通道不受影响
 */
#ifndef TOY_SERIAL_LOG_H
#define TOY_SERIAL_LOG_H

#include "ToySerialConfig.h"
#include "HalSerial.h"

#define ToyBootMarkBoot(Text)  HalSerialBootMarkChannel(TOY_SLOG_BOOT, (Text))
#define ToyBootMarkUsb(Text)   HalSerialBootMarkChannel(TOY_SLOG_USB, (Text))
#define ToyBootMarkSmp(Text)   HalSerialBootMarkChannel(TOY_SLOG_SMP, (Text))

#define ToyLogBoot(Text)       HalSerialWriteChannel(TOY_SLOG_BOOT, (Text))
#define ToyLogUsb(Text)        HalSerialWriteChannel(TOY_SLOG_USB, (Text))
#define ToyLogSmp(Text)        HalSerialWriteChannel(TOY_SLOG_SMP, (Text))
#define ToyLogGui(Text)        HalSerialWriteChannel(TOY_SLOG_GUI, (Text))
#define ToyLogNet(Text)        HalSerialWriteChannel(TOY_SLOG_NET, (Text))
#define ToyLogFs(Text)         HalSerialWriteChannel(TOY_SLOG_FS, (Text))
#define ToyLogMem(Text)        HalSerialWriteChannel(TOY_SLOG_MEM, (Text))
#define ToyLogDrv(Text)        HalSerialWriteChannel(TOY_SLOG_DRV, (Text))
#define ToyLogMisc(Text)       HalSerialWriteChannel(TOY_SLOG_MISC, (Text))

#define ToyLogBootHex32(V)     HalSerialWriteChannelHex32(TOY_SLOG_BOOT, (V))
#define ToyLogUsbHex32(V)      HalSerialWriteChannelHex32(TOY_SLOG_USB, (V))
#define ToyLogSmpHex32(V)      HalSerialWriteChannelHex32(TOY_SLOG_SMP, (V))
#define ToyLogSmpHex64(V)      HalSerialWriteChannelHex64(TOY_SLOG_SMP, (V))
#define ToyLogNetHex32(V)      HalSerialWriteChannelHex32(TOY_SLOG_NET, (V))
#define ToyLogFsHex32(V)       HalSerialWriteChannelHex32(TOY_SLOG_FS, (V))
#define ToyLogMemHex32(V)      HalSerialWriteChannelHex32(TOY_SLOG_MEM, (V))
#define ToyLogDrvHex32(V)      HalSerialWriteChannelHex32(TOY_SLOG_DRV, (V))
#define ToyLogMiscHex32(V)     HalSerialWriteChannelHex32(TOY_SLOG_MISC, (V))
#define ToyLogMiscHex64(V)     HalSerialWriteChannelHex64(TOY_SLOG_MISC, (V))

#endif
