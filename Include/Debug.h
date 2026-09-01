/*
 * Debug.h — 内核调试输出开关
 *
 * 编译：make DEBUG=1 或 ./build.sh 时 DEBUG=1
 * 默认 TOY_DEBUG=0：DebugWrite/DebugHex* 编译为空操作。
 * Shell 命令输出请继续用 ConsoleWrite（不受此开关影响）。
 */
#ifndef TOY_DEBUG_H
#define TOY_DEBUG_H

#include "Serial.h"

#ifndef TOY_DEBUG
#define TOY_DEBUG 0
#endif

#if TOY_DEBUG
#define DebugWrite(Text)   SerialWrite(Text)
#define DebugHex32(Value)  SerialHex32(Value)
#define DebugHex64(Value)  SerialHex64(Value)
#else
#define DebugWrite(Text)   ((void)0)
#define DebugHex32(Value)  ((void)0)
#define DebugHex64(Value)  ((void)0)
#endif

#endif
