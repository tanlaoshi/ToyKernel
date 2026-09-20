/*
 * SettingsUiPrivate.h — SettingsUi 内部共享头（仅 Common/Services/SettingsUi 使用）
 *
 * 禁止 User 程序、HAL、Core 包含本文件。
 * 源文件在 Common/Services/SettingsUi/（核心 SettingsUi.c）。
 */
#ifndef SETTINGS_UI_PRIVATE_H
#define SETTINGS_UI_PRIVATE_H

#include "SettingsUi.h"
#include "Gui.h"
#include "Theme.h"
#include "Font.h"
#include "UI.h"
#include "Hal.h"
#include "HalConsole.h"
#include "Debug.h"
#include "Locale.h"
#include "BootInfo.h"

/* ===== 类型（从 SettingsUi.c 搬入；布局不变） ===== */
typedef enum {
    SETTINGS_CAT_DESKTOP = 0,
    SETTINGS_CAT_SHELL,
    SETTINGS_CAT_FONT,
    SETTINGS_CAT_DISPLAY,
    SETTINGS_CAT_LANGUAGE,
    SETTINGS_CAT_SCALE,
    SETTINGS_CAT_THEME,
    SETTINGS_CAT_COUNT
} SETTINGS_CAT;

typedef struct {
    const char *Label;
    UINT32      Color;
} SETTINGS_COLOR;

typedef struct {
    const char *Label;
    UINT32      W;
    UINT32      H;
} SETTINGS_MODE;

typedef struct {
    UINT32 X;
    UINT32 Y;
    UINT32 W;
    UINT32 H;
    int    Kind;   /* 0=左栏 1=中栏 2=右钮 */
    int    Index;
} SETTINGS_HIT;

/* ===== 宏（值不变；颜色/缩放数组长度写死，供跨 TU 使用） ===== */
#define SETTINGS_HIT_MAX  48
#define SETTINGS_SIDE_W   128u
#define SETTINGS_SIDE_BG  0x00A0A8B0u
#define SETTINGS_PREV_BG  0x00D8D8E0u
#define SETTINGS_SB_W     12u

#define DESKTOP_COLOR_COUNT 6
/* 首项 Wallpaper：Color 哨兵，Apply 时开壁纸而非铺纯色 */
#define DESKTOP_COLOR_WALLPAPER 0xFFFFFFFFu
#define SHELL_COLOR_COUNT   5
#define SCALE_COUNT         4
#define THEME_CHOICE_COUNT  3
#define MODES_FALLBACK_COUNT 5

/* ===== 全局（定义在 SettingsUi.c） ===== */
extern SETTINGS_CAT gCat;
extern int gItemSel;
extern int gItemScroll;
extern int gDisplayHint; /* 0=无；1=须重启；2=已热切 */
extern SETTINGS_HIT gSetHits[SETTINGS_HIT_MAX];
extern int gSetHitCount;
extern int gSetHoverKind;
extern int gSetHoverIdx;
extern int gSetPressKind;
extern int gSetPressIdx;

/* 布局（Paint 写入，Click/Pointer 读取） */
extern UINT32 gSetSideX;
extern UINT32 gSetSideY;
extern UINT32 gSetSideW;
extern UINT32 gSetSideRow0;
extern UINT32 gSetSideLineH;
extern UINT32 gSetListX;
extern UINT32 gSetListTop;
extern UINT32 gSetListRowW;
extern UINT32 gSetListLineH;
extern int gSetListVisible;
extern UINT32 gSetSbX;
extern UINT32 gSetSbY;
extern UINT32 gSetSbW;
extern UINT32 gSetSbH;
extern int gSetSbVisible;
extern UINT32 gSetPrevX;
extern UINT32 gSetPrevW;

extern const SETTINGS_COLOR gDesktopColors[DESKTOP_COLOR_COUNT];
extern const SETTINGS_COLOR gShellColors[SHELL_COLOR_COUNT];
extern const SETTINGS_MODE gModesFallback[MODES_FALLBACK_COUNT];
extern SETTINGS_MODE gModes[BOOT_VIDEO_MODE_MAX];
extern char gModeLabels[BOOT_VIDEO_MODE_MAX][16];
extern int gModeCount;
extern int gModesReady;
extern const UINT32 gScales[SCALE_COUNT];

/* ===== 共享函数（原 static；定义见各辅助 .c） ===== */

/* SettingsUiModel.c */
const char *CatLabel(SETTINGS_CAT C);
void EnsureDisplayModes(void);
int ModeCount(void);
int ItemCount(void);
void ItemLabel(int Idx, char *Out, int OutMax);
int CurrentItemIndex(void);

/* SettingsUiApply.c */
void FormatNowDisplay(char *Out, UINTN Max);
void FormatUxU(char *Out, UINTN Max, UINT32 A, UINT32 B);

/* SettingsUi.c（核心） */
int FocusSettingsWindow(void);
void HitClear(void);
void HitAdd(UINT32 X, UINT32 Y, UINT32 W, UINT32 H, int Kind, int Index);
void ClampItemScroll(void);
void SelectCategory(SETTINGS_CAT C);

/* SettingsUiApply.c */
void ApplyDesktopColor(int Index);
void ApplyShellColor(int Index);
void ApplyFont(int Index);
void ApplyScaleChoice(int Index);
void ApplyDisplayChoice(int Index);
void ApplyThemeChoice(int Index);
void ApplyItem(int Idx);

/* SettingsUiPaint.c */
void DrawDetail(UINT32 X, UINT32 Y, UINT32 W, UINT32 H);
void PaintMenu(void);

#endif
