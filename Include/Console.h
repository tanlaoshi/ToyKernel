/*
 * Console.h — 交互式 Shell 接口
 *
 * 双通道输出：串口 + 帧缓冲文字。命令通过 ConsoleRegister 动态注册。
 * HalSerialInit + VideoSet 之后即可 ConsoleWrite，无需 ConsoleInit。
 */
#ifndef CONSOLE_H
#define CONSOLE_H

#include "BootTypes.h"

void ConsoleInit(void);
void ConsoleRegisterBuiltins(void);
void ConsoleRegister(const char *Name, const char *Help,
                     void (*Handler)(int Argc, char **Argv));
void ConsoleWrite(const char *Text);
void ConsoleWriteLen(const char *Data, UINTN Len);
void ConsoleHex32(UINT32 Value);
void ConsoleHex64(UINT64 Value);
void ConsoleOnChar(char C);
void ConsoleOnEnter(void);
void ConsoleOnBackspace(void);
void ConsoleCancelInput(void);
void ConsoleBindFocus(void);
/* PR-D3：新开 Shell 窗后打印欢迎语与提示符 */
void ConsoleOnShellOpened(void);
/* PR-G8：按窗下标重画 Shell 客户区（主题合成用，不 Raise） */
void ConsolePaintShellWindow(int Idx);
/* PR-D5：ThemeApply/GuiRedraw 后立刻重画所有 Shell，避免等再点标题栏 */
void ConsoleRepaintShellWindows(void);

/* PR-G2：焦点切换时保存/恢复当前窗输入行（由 GuiFocusSave/Apply 调用） */
void ConsoleFocusSave(void);
void ConsoleFocusLoad(void);

/* exec/runuser 成功后延迟提示符，进程退出时 ConsoleShowPrompt */
void ConsoleWaitPrompt(void);
void ConsoleShowPrompt(void);

/* listen 等后台任务期间抑制 toyos>；ConsoleNotify 只换行输出 */
void ConsoleSuspendPrompt(void);
void ConsoleResumePrompt(void);
int  ConsolePromptSuspended(void);
void ConsoleNotify(const char *Text);

/* PR-A9：virt 串口 Shell（轮询 HalSerial + HalTimerPoll） */
void ConsoleSerialRun(void);

#endif
