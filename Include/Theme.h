/*
 * Theme.h — 桌面/Shell 主题（PR-D2）
 *
 * 默认值与改前观感一致；Settings（D5）经 setter + ThemeApply 即时生效。
 * 持久化见 PR-D6。
 */
#ifndef THEME_H
#define THEME_H

#include "BootTypes.h"

void ThemeInit(void);

UINT32 ThemeDesktopBg(void);
UINT32 ThemeShellClientBg(void);
UINT32 ThemeFontId(void);

void ThemeSetDesktopBg(UINT32 Color);
void ThemeSetShellClientBg(UINT32 Color);
/* 同步 FontSetById；越界返回 -1 */
int ThemeSetFontId(UINT32 Id);

/* 应用 FontId，并把 Shell 客户区底色刷到已开窗，再 GuiRedraw */
void ThemeApply(void);

#endif
