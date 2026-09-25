/*
 * TtyUi.h — 串口会话窗（PR-GUI-tty-win）
 *
 * 独立窗：键入 → HalSerial TX + 本地回显；焦点期 RX 进缓冲。
 * 不是 HalSerialLogText / boot GOP 镜像。
 */
#ifndef TTY_UI_H
#define TTY_UI_H

#include "BootTypes.h"

void TtyUiOpen(void);
void TtyUiClose(void);
void TtyUiRepaint(void);
void TtyUiPaintFocused(void);
void TtyUiOnClick(UINT32 X, UINT32 Y);
void TtyUiOnChar(char C);
void TtyUiOnEnter(void);
void TtyUiOnBackspace(void);
void TtyUiOnEscape(void);
/* 串口 RX（TasksShell 焦点路由） */
void TtyUiOnRxChar(char C);
int TtyUiIsFocused(void);

#endif
