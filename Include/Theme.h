/*
 * Theme.h — 桌面/Shell 主题（PR-D2）+ FAT 持久化（PR-D6）+ 分辨率偏好（PR-D7）
 *
 * 偏好键 desktop/shell/font/mode：优先 TOYOS.DB（PR-DB1）；仍写 THEME.CFG
 * 供 ToyBoot GOP SetMode（重启生效）。ThemeLoad 在 GuiInit 前；ThemeApply 末尾 ThemeSave。
 */
#ifndef THEME_H
#define THEME_H

#include "BootTypes.h"

#define THEME_CFG_PATH  "THEME.CFG"

void ThemeInit(void);

UINT32 ThemeDesktopBackground(void);
UINT32 ThemeShellClientBackground(void);
UINT32 ThemeSettingsClientBackground(void);
UINT32 ThemeFontId(void);

void ThemeSetDesktopBackground(UINT32 Color);
void ThemeSetShellClientBackground(UINT32 Color);
/* 同步 FontSetById；越界返回 -1 且保持先前字体 */
int ThemeSetFontId(UINT32 Id);
/* FontReloadAssets 后钳制 theme font id */
void ThemeClampFontId(void);

/*
 * PR-D7：下次启动分辨率偏好。W=H=0 表示未设置（Boot 走默认打分）。
 * 不改变当前帧缓冲；调用方 ThemeSave 后提示重启。
 */
UINT32 ThemeDisplayWidth(void);
UINT32 ThemeDisplayHeight(void);
int ThemeHasDisplayPref(void);
void ThemeSetDisplayMode(UINT32 Width, UINT32 Height);
void ThemeClearDisplayMode(void);

/* 应用 FontId 与窗属性，再 GuiComposeThemeScene 一次提交；并 ThemeSave */
void ThemeApply(void);

/* PR-DB1/D6/D7：优先 TOYOS.DB，否则 THEME.CFG；成功 0，皆无 -1（保持当前值） */
int ThemeLoad(void);
/* 写 TOYOS.DB + THEME.CFG；CFG 失败则 -1 */
int ThemeSave(void);

#endif
