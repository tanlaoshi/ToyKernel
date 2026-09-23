/*
 * Theme.h — 桌面/Shell 主题（PR-D2）+ FAT 持久化（PR-D6）+ 分辨率偏好（PR-D7）
 *
 * 偏好键 desktop/shell/font/mode/wallpaper/theme：优先 TOYOS.DB（PR-DB1）；仍写 THEME.CFG
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
/* 1=铺 WALL.BMP；0=纯色 ThemeDesktopBackground（Settings 选色后关壁纸） */
int ThemeWallpaperEnabled(void);
void ThemeSetWallpaper(int Enabled);

/* PR-GUI-tech-1：色板 id；0=classic（默认），1=tech。不改旧 getter 签名。 */
#define THEME_PALETTE_DEFAULT 0
#define THEME_PALETTE_TECH    1
int ThemeThemeId(void);
void ThemeSetThemeId(int Id);
/* PR-GUI-tech-3：tech 对角渐变桌面（wallpaper=0 时）；0=关 */
int ThemeDesktopGradientEnabled(void);
void ThemeSetDesktopGradient(int Enabled);
UINT32 ThemeDesktopGradientAt(UINT32 X, UINT32 Y, UINT32 Sw, UINT32 Sh);

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

/*
 * PR-GUI-effects：美化效果级别（默认 low；high = 全开美化）。
 * Is* 由级别派生；DB 键 theme.effects 在第 3 步。
 */
typedef enum {
    THEME_EFFECT_MINIMAL = 0, /* 无美化（边框三态仍开） */
    THEME_EFFECT_LOW = 1,     /* 同 minimal：只保留边框（硬边字体） */
    THEME_EFFECT_MEDIUM = 2,  /* + 半透明 + 标题渐变 + 点阵灰度边 */
    THEME_EFFECT_HIGH = 3     /* + 阴影 + 淡入淡出 */
} THEME_EFFECT_LEVEL;

THEME_EFFECT_LEVEL ThemeGetEffectLevel(void);
void ThemeSetEffectLevel(THEME_EFFECT_LEVEL Level);
int ThemeIsShadowEnabled(void);
int ThemeIsFadeEnabled(void);
int ThemeIsAlphaEnabled(void);
int ThemeIsGradientEnabled(void);
/* PR-GUI-l2-font：medium 及以上开点阵灰度边；minimal/low 硬边 */
int ThemeIsFontSmoothEnabled(void);
/* 供 DB/Shell/CFG；未知级别回 "high" */
const char *ThemeEffectLevelName(THEME_EFFECT_LEVEL Level);

/* PR-GUI-tech-2：文字 / 桌面图标 / 菜单 / 面板 / 滚动条（只追加；classic 返回值不变） */
UINT32 ThemeShellText(void);
UINT32 ThemeShellPrompt(void);
UINT32 ThemeIconText(void);
UINT32 ThemeIconBorder(void);
UINT32 ThemeIconSelect(void);
UINT32 ThemeClockText(void);
UINT32 ThemeStartButtonText(void);
UINT32 ThemeMenuBorder(void);
UINT32 ThemeMenuText(void);
UINT32 ThemeMenuSep(void);
UINT32 ThemeIconFallbackPower(void);
UINT32 ThemeIconFallbackReboot(void);
UINT32 ThemeText(void);
UINT32 ThemeTextMuted(void);
UINT32 ThemeTextAccent(void);
UINT32 ThemeTextOnAccent(void);
UINT32 ThemePanelSideBackground(void);
UINT32 ThemePanelDetailBackground(void);
UINT32 ThemePanelSeparator(void);
UINT32 ThemeScrollTrack(void);
UINT32 ThemeScrollBorder(void);
UINT32 ThemeScrollThumb(void);
UINT32 ThemeDialogFace(void);
UINT32 ThemeDialogBorder(void);

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
