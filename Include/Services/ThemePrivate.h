/*
 * ThemePrivate.h — Theme 内部共享头（仅 Common/Services/Theme 使用）
 *
 * 禁止 User 程序、HAL、Core 包含本文件。
 * 源文件在 Common/Services/Theme/（核心 Theme.c）。
 * 对外 API 仍在 Theme.h。
 */
#ifndef THEME_PRIVATE_H
#define THEME_PRIVATE_H

#include "Theme.h"
#include "UI.h"
#include "Font.h"
#include "Gui.h"
#include "FileSystem.h"
#include "Db.h"
#include "Hal.h"
#include "HalConsole.h"
#include "Debug.h"
#include "ToySerialLog.h"
#include "VirtualMemory.h"
#include "BootInfo.h"

/* ===== 全局（定义在 Theme.c） ===== */
extern UINT32 gDesktopBg;
extern UINT32 gShellClientBg;
extern UINT32 gFontId;
extern UINT32 gModeW;
extern UINT32 gModeH;
extern UINT32 gThemeUiScale; /* 50 / 100 / 150 / 200；与 Video gUiScale 区分 */
extern UINT32 gFadeSteps;    /* PR-GUI-l3-fade；0=关 */
extern THEME_EFFECT_LEVEL gEffectLevel; /* PR-GUI-effects；默认 HIGH */
extern int gWallpaper;       /* 1=BMP 壁纸；0=纯色（开机默认 Grey） */
extern int gThemeId;         /* DEFAULT | TECH | MODERN */
extern int gDesktopGrad;     /* 1=tech 对角渐变；仅 theme=tech 有意义 */
extern int gScalePrefSet;    /* 1=DB/CFG 已有 scale= */
extern int gScaleUserSet;    /* 1=用户在 Settings 选过缩放（scalesrc=user） */
extern int gWallpaperPrefSet; /* 1=DB/CFG 已有 wallpaper= */

/* ThemeTech.c */
void ThemeTechApplyDefaults(void);
void ThemeTechApplyColors(void);
int ThemeTechParseName(const char *Val, int *OutId);
const char *ThemeTechName(int Id);

/* ThemeModern.c — PR-UI-palette */
void ThemeModernApplyDefaults(void);
void ThemeModernApplyColors(void);
UINT32 ThemeModernSettingsClientBackground(void);
UINT32 ThemeModernWindowTitleFocus(void);
UINT32 ThemeModernWindowTitleIdle(void);
UINT32 ThemeModernWindowTitleHover(void);
UINT32 ThemeModernWindowBorderFocus(void);
UINT32 ThemeModernWindowBorderIdle(void);
UINT32 ThemeModernWindowBorderHover(void);
UINT32 ThemeModernWindowTitleText(void);
UINT32 ThemeModernCloseButton(void);
UINT32 ThemeModernTaskbarBackground(void);
UINT32 ThemeModernTaskbarButton(void);
UINT32 ThemeModernTaskbarButtonActive(void);
UINT32 ThemeModernControlFace(void);
UINT32 ThemeModernControlBorder(void);
UINT32 ThemeModernControlAccent(void);
UINT32 ThemeModernWindowShadowColor(void);
UINT32 ThemeModernButtonFaceNormal(void);
UINT32 ThemeModernButtonFaceHover(void);
UINT32 ThemeModernButtonFacePressed(void);
UINT32 ThemeModernButtonFaceDisabled(void);
UINT32 ThemeModernButtonBorderNormal(void);
UINT32 ThemeModernButtonBorderHover(void);
UINT32 ThemeModernButtonBorderPressed(void);
UINT32 ThemeModernButtonBorderDisabled(void);
UINT32 ThemeModernButtonTextDisabled(void);
UINT32 ThemeModernShellText(void);
UINT32 ThemeModernShellPrompt(void);
UINT32 ThemeModernIconText(void);
UINT32 ThemeModernIconBorder(void);
UINT32 ThemeModernIconSelect(void);
UINT32 ThemeModernClockText(void);
UINT32 ThemeModernStartButtonText(void);
UINT32 ThemeModernMenuBorder(void);
UINT32 ThemeModernMenuText(void);
UINT32 ThemeModernMenuSep(void);
UINT32 ThemeModernText(void);
UINT32 ThemeModernTextMuted(void);
UINT32 ThemeModernTextAccent(void);
UINT32 ThemeModernTextOnAccent(void);
UINT32 ThemeModernPanelSideBackground(void);
UINT32 ThemeModernPanelDetailBackground(void);
UINT32 ThemeModernPanelSeparator(void);
UINT32 ThemeModernScrollTrack(void);
UINT32 ThemeModernScrollBorder(void);
UINT32 ThemeModernScrollThumb(void);
UINT32 ThemeModernDialogFace(void);
UINT32 ThemeModernDialogBorder(void);
UINT32 ThemeModernListSelect(void);

/* ===== 共享帮手（原 static） ===== */

/* 与 Video NormalizeUiScale 同逻辑；原 Theme static，避免与 Video 撞符号 */
static inline UINT32 NormalizeUiScale(UINT32 Percent) {
    if (Percent <= 75) {
        return 50;
    }
    if (Percent <= 125) {
        return 100;
    }
    if (Percent <= 175) {
        return 150;
    }
    return 200;
}

/* Theme.c */
UINT32 ThemeCompactFontId(void);

/* ThemeParse.c */
int IsSpace(char C);
int HexVal(char C);
int ParseHexU32(const char *S, UINT32 *Out);
int ParseDecU32(const char *S, UINT32 *Out, const char **End);
int ParseModeValue(const char *S, UINT32 *W, UINT32 *H);
const char *ValueAfterKey(const char *Line, const char *Key);
void ApplyLine(const char *Line);
int ApplyDbKey(const char *Key);

/* ThemeSave.c / ThemeCfg.c / ThemeSaveFmt.c */
void PutHex6(char *Dst, UINT32 Color);
void PutDec(char *Dst, UINT32 V, UINTN *Len);
int ThemeLoadFromCfg(void);
int ThemeOverlayModeFromCfg(void);

/* PR-F-theme-1：ThemeSave 拆分 */
typedef struct THEME_SAVE_VALS {
    char FontVal[8];
    char ModeVal[24];
    char ScaleVal[8];
    char FadeVal[8];
    char WallVal[2];
    char GradVal[2];
} THEME_SAVE_VALS;

void ThemeSaveFillVals(THEME_SAVE_VALS *V);
UINTN ThemeSaveBuildCfg(char *Buf, UINTN Cap, const THEME_SAVE_VALS *V);
int ThemeSaveWriteCfg(const char *Buf, UINTN N);
int ThemeSaveWriteDb(const THEME_SAVE_VALS *V);

#endif
