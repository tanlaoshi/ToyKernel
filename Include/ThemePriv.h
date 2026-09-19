/*
 * ThemePriv.h — Theme 内部共享头（仅 Common/Services/Theme 使用）
 *
 * 禁止 User 程序、HAL、Common/Core 包含本文件。
 * 源文件在 Common/Services/Theme/（核心 Theme.c）。
 * 对外 API 仍在 Theme.h。
 */
#ifndef THEME_PRIV_H
#define THEME_PRIV_H

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

/* ThemeSave.c / ThemeCfg.c */
void PutHex6(char *Dst, UINT32 Color);
void PutDec(char *Dst, UINT32 V, UINTN *Len);
int ThemeLoadFromCfg(void);
int ThemeOverlayModeFromCfg(void);

#endif
