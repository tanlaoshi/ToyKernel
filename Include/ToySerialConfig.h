/*
 * ToySerialConfig.h — 串口总开关 + 分模块 quiet（编译期）
 *
 * 总开关 TOY_SERIAL：
 *   0 → 不 Probe/不 Init UART；SerialWrite/RX 全空；无任何串口 TX
 *   1 → 正常 Probe；各模块由 TOY_SERIAL_* 决定是否往 UART 打
 *
 * 分模块（仅 TOY_SERIAL=1 时有效；0=该模块 quiet）：
 *   BOOT USB SMP GUI NET FS MEM DRV MISC
 *
 * 构建：
 *   ./build.sh SERIAL=0
 *   ./build.sh SERIAL_USB=0 SERIAL_SMP=0
 *   NO_COM1=1 仍可用，等价于 SERIAL=0（兼容旧课堂开关）
 *
 * 说明：quiet / SERIAL=0 只关 UART（及 WriteChannel 无 COM 时的 GOP 镜像）；
 * ring 与 BootMark 屏上黄字仍可出，便于真机 PHOTO。
 * Shell 回显走 HalSerialWrite（MISC）；SERIAL_MISC=0 时旁路 TX 也 quiet。
 */
#ifndef TOY_SERIAL_CONFIG_H
#define TOY_SERIAL_CONFIG_H

#ifndef TOY_SERIAL
#define TOY_SERIAL 0
#endif

#if !TOY_SERIAL
#define TOY_SERIAL_BOOT 0
#define TOY_SERIAL_USB  0
#define TOY_SERIAL_SMP  0
#define TOY_SERIAL_GUI  0
#define TOY_SERIAL_NET  0
#define TOY_SERIAL_FS   0
#define TOY_SERIAL_MEM  0
#define TOY_SERIAL_DRV  0
#define TOY_SERIAL_MISC 0
#else
#ifndef TOY_SERIAL_BOOT
#define TOY_SERIAL_BOOT 1
#endif
#ifndef TOY_SERIAL_USB
#define TOY_SERIAL_USB 1
#endif
#ifndef TOY_SERIAL_SMP
#define TOY_SERIAL_SMP 1
#endif
#ifndef TOY_SERIAL_GUI
#define TOY_SERIAL_GUI 1
#endif
#ifndef TOY_SERIAL_NET
#define TOY_SERIAL_NET 1
#endif
#ifndef TOY_SERIAL_FS
#define TOY_SERIAL_FS 1
#endif
#ifndef TOY_SERIAL_MEM
#define TOY_SERIAL_MEM 1
#endif
#ifndef TOY_SERIAL_DRV
#define TOY_SERIAL_DRV 1
#endif
#ifndef TOY_SERIAL_MISC
#define TOY_SERIAL_MISC 1
#endif
#endif

/* 通道 ID（与上表对应；供 HalSerialWriteChannel） */
#define TOY_SLOG_BOOT 0
#define TOY_SLOG_USB  1
#define TOY_SLOG_SMP  2
#define TOY_SLOG_GUI  3
#define TOY_SLOG_NET  4
#define TOY_SLOG_FS   5
#define TOY_SLOG_MEM  6
#define TOY_SLOG_DRV  7
#define TOY_SLOG_MISC 8
#define TOY_SLOG_COUNT 9

#endif
