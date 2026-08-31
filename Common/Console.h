/*
 * Console.h — 交互式 Shell 接口
 *
 * 双通道输出：串口 + 帧缓冲文字。命令通过 ConsoleRegister 动态注册。
 * SerialInit + VideoSet 之后即可 ConsoleWrite，无需 ConsoleInit。
 */
#ifndef CONSOLE_H
#define CONSOLE_H

#include "BootConfig.h"

void ConsoleInit(void);
void ConsoleRegister(const char *Name, const char *Help,
                     void (*Handler)(int Argc, char **Argv));
void ConsoleWrite(const char *Text);
void ConsoleHex32(UINT32 Value);
void ConsoleHex64(UINT64 Value);
void ConsoleOnChar(char C);
void ConsoleOnEnter(void);
void ConsoleOnBackspace(void);
void ConsoleBindFocus(void);

#endif
