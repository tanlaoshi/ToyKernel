/*
 * Theme.h — 桌面/Shell 主题（PR-D2）+ FAT 持久化（PR-D6）+ 分辨率偏好（PR-D7）
 *
 * 偏好键 desktop/shell/font/mode：优先 TOYOS.DB（PR-DB1）；仍写 THEME.CFG
 * 供 ToyBoot GOP SetMode（冷启动）。ThemeLoad 在 GuiInit 前；ThemeApply 末尾 ThemeSave。
 * PR-G-hotres：QEMU 上 ThemeApplyDisplayLive 可运行时切分辨率（Bochs DISPI）。
 */
#ifndef THEME_H
#define THEME_H

#include "BootTypes.h"

#define THEME_CFG_PATH  "THEME.CFG"

void ThemeInitialize(void);

UINT32 ThemeDesktopBackground(void);
UINT32 ThemeShellClientBackground(void);
UINT32 ThemeSettingsClientBackground(void);
UINT32 ThemeFontId(void);

/*
 * PR-GUI-l1：窗框/任务栏/控件配色与客户区内边距（只扩展 getter；暂不进 THEME.CFG）。
 * 三态：焦点 / 空闲 / 悬停。
 */
UINT32 ThemeWindowTitleFocus(void);
UINT32 ThemeWindowTitleIdle(void);
UINT32 ThemeWindowTitleHover(void);
UINT32 ThemeWindowBorderFocus(void);
UINT32 ThemeWindowBorderIdle(void);
UINT32 ThemeWindowBorderHover(void);
UINT32 ThemeWindowTitleText(void);
UINT32 ThemeCloseButton(void);
UINT32 ThemeTaskbarBackground(void);
UINT32 ThemeTaskbarButton(void);
UINT32 ThemeTaskbarButtonActive(void);
UINT32 ThemeControlFace(void);
UINT32 ThemeControlBorder(void);
UINT32 ThemeControlAccent(void);
UINT32 ThemeClientPadding(void);
/* PR-GUI-alpha：半透明面板不透明度（0..255）；暂不进 THEME.CFG */
UINT8 ThemeTaskbarAlpha(void);
UINT8 ThemeMenuPanelAlpha(void);
/* PR-GUI-l2-shadow：窗外 drop shadow（右/下）；暂不进 THEME.CFG */
UINT32 ThemeWindowShadowSize(void);
UINT8 ThemeWindowShadowMaxAlpha(void);
UINT32 ThemeWindowShadowColor(void);
/*
 * PR-GUI-l2-gradient：标题栏底色（相对顶色加深）；暂不进 THEME.CFG。
 * 顶色仍走 ThemeWindowTitleFocus/Idle/Hover；绘制侧逐行插值到本色。
 */
UINT32 ThemeWindowTitleGradientBottom(UINT32 Top);
/*
 * PR-GUI-l3-fade：开关窗淡入淡出中间帧数；0=关动画。
 * THEME.CFG / DB 键 fade=（默认 6；真机卡可设 fade=0）。
 */
UINT32 ThemeWindowFadeSteps(void);
void ThemeSetWindowFadeSteps(UINT32 Steps);

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
/* UI 整体缩放：50 / 100 / 150 / 200（高分放大、低分缩小） */
UINT32 ThemeUiScale(void);
void ThemeSetUiScale(UINT32 Percent);
/* 运行时改缩放并重配后缓冲/桌面；失败 -1 */
int ThemeApplyUiScaleLive(UINT32 Percent);
/*
 * PR-G-hotres：尝试运行时切到 WxH（QEMU Bochs VGA）。
 * 成功 0 并重绘桌面；不支持/失败 -1（调用方仍可 ThemeSave 走重启路径）。
 */
int ThemeApplyDisplayLive(UINT32 Width, UINT32 Height);

/* 应用 FontId 与窗属性，再 GuiComposeThemeScene 一次提交；并 ThemeSave */
void ThemeApply(void);

/* PR-DB1/D6/D7：优先 TOYOS.DB，否则 THEME.CFG；成功 0，皆无 -1（保持当前值） */
int ThemeLoad(void);
/* 写 TOYOS.DB + THEME.CFG；CFG 失败则 -1 */
int ThemeSave(void);

#endif
