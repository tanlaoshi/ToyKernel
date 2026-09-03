/*
 * Theme.h — 桌面/Shell 主题（PR-D2）+ FAT 持久化（PR-D6）
 *
 * 默认值与改前观感一致；Settings（D5）经 setter + ThemeApply 即时生效。
 * THEME.CFG：desktop/shell/font；ThemeLoad 在 GuiInit 前；ThemeApply 末尾 ThemeSave。
 */
#ifndef THEME_H
#define THEME_H

#include "BootTypes.h"

#define THEME_CFG_PATH  "THEME.CFG"

void ThemeInit(void);

UINT32 ThemeDesktopBg(void);
UINT32 ThemeShellClientBg(void);
UINT32 ThemeFontId(void);

void ThemeSetDesktopBg(UINT32 Color);
void ThemeSetShellClientBg(UINT32 Color);
/* 同步 FontSetById；越界返回 -1 */
int ThemeSetFontId(UINT32 Id);

/* 应用 FontId，并把 Shell 客户区底色刷到已开窗，再 GuiRedraw；并尝试 ThemeSave */
void ThemeApply(void);

/* PR-D6：从 FAT 读 THEME.CFG；成功 0，无文件/解析失败 -1（保持当前值） */
int ThemeLoad(void);
/* 写 THEME.CFG；成功 0，失败 -1 */
int ThemeSave(void);

#endif
