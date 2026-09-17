/*
 * ToySerialConfig.h — 串口 + 屏幕（boot GOP）编译期开关（PR-K-log-switch）
 *
 * 串口总开关 TOY_SERIAL：
 *   0 → 不 Probe/不 Init UART；SerialWrite/RX 全空；无任何串口 TX
 *   1 → 正常 Probe；各模块由 TOY_SERIAL_* 决定是否往 UART 打
 *
 * 屏幕总开关 TOY_SCREEN_LOG：
 *   0 → boot GOP 不上字（ring 仍收；Mute/Mirror API 仍在）
 *   1 → 各模块 SCREEN_LOG_* 上滚
 *
 * 分模块（仅对应总开=1 时有效；0=该模块 quiet）：
 *   BOOT USB SMP GUI NET FS MEM DRV MISC
 *
 * 构建：
 *   ./build.sh SERIAL=0
 *   ./build.sh SERIAL_USB=0 SERIAL_SMP=0
 *   ./build.sh SCREEN_LOG=0
 *   ./build.sh SCREEN_LOG_SMP=1          # 课堂默认 SMP 不上屏；可打开
 *   NO_COM1=1 仍可用，等价于 SERIAL=0（兼容旧课堂开关）
 *
 * 说明：SERIAL=0 只关 UART；SCREEN_LOG=0 关 boot GOP 上滚。
 * ring 始终收。运行时 Mute / Mirror 仍是真机安全阀。
 * Shell 回显走 HalSerialWrite（MISC）+ Console FB，不纳入 SCREEN_LOG（本柱不做）。
 */
#ifndef TOY_SERIAL_CONFIG_H
#define TOY_SERIAL_CONFIG_H

#ifndef TOY_SERIAL
#define TOY_SERIAL 0
#endif

#if !TOY_SERIAL
#ifdef TOY_SERIAL_BOOT
#undef TOY_SERIAL_BOOT
#endif
#ifdef TOY_SERIAL_USB
#undef TOY_SERIAL_USB
#endif
#ifdef TOY_SERIAL_SMP
#undef TOY_SERIAL_SMP
#endif
#ifdef TOY_SERIAL_GUI
#undef TOY_SERIAL_GUI
#endif
#ifdef TOY_SERIAL_NET
#undef TOY_SERIAL_NET
#endif
#ifdef TOY_SERIAL_FS
#undef TOY_SERIAL_FS
#endif
#ifdef TOY_SERIAL_MEM
#undef TOY_SERIAL_MEM
#endif
#ifdef TOY_SERIAL_DRV
#undef TOY_SERIAL_DRV
#endif
#ifdef TOY_SERIAL_MISC
#undef TOY_SERIAL_MISC
#endif
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

/*
 * 屏幕：课堂默认 BOOT/USB/FS/GUI 上屏；SMP/MEM/NET/DRV/MISC 默认 quiet，
 * 避免 MADT/细日志刷满 bring-up 视口（与 cont 同文策略一致）。
 */
#ifndef TOY_SCREEN_LOG
#define TOY_SCREEN_LOG 1
#endif

#if !TOY_SCREEN_LOG
#ifdef TOY_SCREEN_LOG_BOOT
#undef TOY_SCREEN_LOG_BOOT
#endif
#ifdef TOY_SCREEN_LOG_USB
#undef TOY_SCREEN_LOG_USB
#endif
#ifdef TOY_SCREEN_LOG_SMP
#undef TOY_SCREEN_LOG_SMP
#endif
#ifdef TOY_SCREEN_LOG_GUI
#undef TOY_SCREEN_LOG_GUI
#endif
#ifdef TOY_SCREEN_LOG_NET
#undef TOY_SCREEN_LOG_NET
#endif
#ifdef TOY_SCREEN_LOG_FS
#undef TOY_SCREEN_LOG_FS
#endif
#ifdef TOY_SCREEN_LOG_MEM
#undef TOY_SCREEN_LOG_MEM
#endif
#ifdef TOY_SCREEN_LOG_DRV
#undef TOY_SCREEN_LOG_DRV
#endif
#ifdef TOY_SCREEN_LOG_MISC
#undef TOY_SCREEN_LOG_MISC
#endif
#define TOY_SCREEN_LOG_BOOT 0
#define TOY_SCREEN_LOG_USB  0
#define TOY_SCREEN_LOG_SMP  0
#define TOY_SCREEN_LOG_GUI  0
#define TOY_SCREEN_LOG_NET  0
#define TOY_SCREEN_LOG_FS   0
#define TOY_SCREEN_LOG_MEM  0
#define TOY_SCREEN_LOG_DRV  0
#define TOY_SCREEN_LOG_MISC 0
#else
#ifndef TOY_SCREEN_LOG_BOOT
#define TOY_SCREEN_LOG_BOOT 1
#endif
#ifndef TOY_SCREEN_LOG_USB
#define TOY_SCREEN_LOG_USB 1
#endif
#ifndef TOY_SCREEN_LOG_SMP
#define TOY_SCREEN_LOG_SMP 0
#endif
#ifndef TOY_SCREEN_LOG_GUI
#define TOY_SCREEN_LOG_GUI 1
#endif
#ifndef TOY_SCREEN_LOG_NET
#define TOY_SCREEN_LOG_NET 0
#endif
#ifndef TOY_SCREEN_LOG_FS
#define TOY_SCREEN_LOG_FS 1
#endif
#ifndef TOY_SCREEN_LOG_MEM
#define TOY_SCREEN_LOG_MEM 0
#endif
#ifndef TOY_SCREEN_LOG_DRV
#define TOY_SCREEN_LOG_DRV 0
#endif
#ifndef TOY_SCREEN_LOG_MISC
#define TOY_SCREEN_LOG_MISC 0
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
