/*
 * ConsolePriv.h — PR-S-console-split-1：Console / ConsoleScroll 内部
 * （仅 Common/Services；User 勿 include）
 */
#ifndef CONSOLE_PRIV_H
#define CONSOLE_PRIV_H

#include "BootTypes.h"

#define SB_LINES 64
#define SB_COLS  120

void ConsoleDrawString(const char *Text, UINT32 Color);
void ConsoleDrawChar(char C, UINT32 Color);

void ConsoleSbReset(void);
void ConsoleSbFeed(const char *Text);
void ConsoleSbFeedChar(char C);
void ConsoleSbBackspace(void);
void ConsoleSbPaint(void);
void ConsoleSbEnsureLive(void);

#endif
