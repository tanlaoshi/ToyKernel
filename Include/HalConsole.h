/*
 * HalConsole.h — 控制台 HAL 门面（Shell 经 Hal.h 使用）
 *
 * 职责划分：
 *   HalConsolePutChar / HalConsoleWriteSerial — 仅串口；Video 未就绪或单字符原子输出
 *   HalConsoleDraw* / HalConsoleGetTextCursor   — 仅帧缓冲文字；不写串口
 *   HalConsoleVideoReady                        — 帧缓冲是否已挂上（Width/Height != 0）
 *   HalConsoleGetChar / HalConsoleHasChar       — 输入（当前委托串口）
 *
 * HalSerialWrite — 调试、网络等模块裸串口，不经 Shell/GUI
 * HalDebugWrite  — 调试日志 -> HalSerialWrite
 *
 * Services/Console.c：HalConsoleWriteSerial + GUI 协调后 HalConsoleDraw*（见 ConsoleWrite）
 */
#ifndef HAL_CONSOLE_H
#define HAL_CONSOLE_H

#include "BootTypes.h"

void HalConsolePutChar(char C);
char HalConsoleGetChar(void);
int HalConsoleHasChar(void);

int HalConsoleVideoReady(void);
void HalConsoleWriteSerial(const char *Text);
void HalConsoleBackspaceSerial(void);

void HalConsoleDrawString(const char *Text, UINT32 Color);
void HalConsoleDrawChar(char C, UINT32 Color);
void HalConsoleEraseLastChar(void);
void HalConsoleGetTextCursor(UINT32 *X, UINT32 *Y);

#endif
