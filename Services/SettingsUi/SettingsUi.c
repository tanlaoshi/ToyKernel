/*
 * SettingsUi.c — Settings 三分栏核心（全局、命中、开窗）
 * 模型：SettingsUiModel.c；应用：SettingsUiApply.c；
 * 绘制：SettingsUiPaint.c；输入：SettingsUiInput.c。
 */
#include "SettingsUiPrivate.h"

SETTINGS_CAT gCat = SETTINGS_CAT_DESKTOP;
int gItemSel;
int gItemScroll;
int gDisplayHint; /* 0=无；1=须重启；2=已热切 */
SETTINGS_HIT gSetHits[SETTINGS_HIT_MAX];
int gSetHitCount;
int gSetHoverKind = -1;
int gSetHoverIdx = -1;
int gSetPressKind = -1;
int gSetPressIdx = -1;

/* 布局（Paint 写入，Click/Pointer 读取） */
UINT32 gSetSideX, gSetSideY, gSetSideW, gSetSideRow0, gSetSideLineH;
UINT32 gSetListX, gSetListTop, gSetListRowW, gSetListLineH;
int gSetListVisible;
UINT32 gSetSbX, gSetSbY, gSetSbW, gSetSbH;
int gSetSbVisible;
UINT32 gSetPrevX, gSetPrevW;

const SETTINGS_COLOR gDesktopColors[DESKTOP_COLOR_COUNT] = {
    { MSG_COLOR_WALLPAPER, DESKTOP_COLOR_WALLPAPER },
    { MSG_COLOR_DARK_GRAY, COLOR_DARK_GRAY },
    { MSG_COLOR_BLUE, COLOR_BLUE },
    { MSG_COLOR_GREEN, COLOR_GREEN },
    { MSG_COLOR_BLACK, COLOR_BLACK },
    { MSG_COLOR_GRAY, COLOR_GRAY },
};

const SETTINGS_COLOR gShellColors[SHELL_COLOR_COUNT] = {
    { MSG_COLOR_LIGHT_GRAY, COLOR_LIGHT_GRAY },
    { MSG_COLOR_WHITE, COLOR_WHITE },
    { MSG_COLOR_CYAN, COLOR_CYAN },
    { MSG_COLOR_YELLOW, COLOR_YELLOW },
    { MSG_COLOR_GRAY, COLOR_GRAY },
};

const SETTINGS_MODE gModesFallback[MODES_FALLBACK_COUNT] = {
    { "800x600",    800,  600 },
    { "1024x768",  1024,  768 },
    { "1280x720",  1280,  720 },
    { "1600x900",  1600,  900 },
    { "1920x1080", 1920, 1080 },
};

SETTINGS_MODE gModes[BOOT_VIDEO_MODE_MAX];
char gModeLabels[BOOT_VIDEO_MODE_MAX][16];
int gModeCount;
int gModesReady;
const UINT32 gScales[SCALE_COUNT] = { 50, 100, 150, 200 };

int FocusSettingsWindow(void) {
    int i;

    if (GuiFocusKind() == GUI_WIN_SETTINGS) {
        return 1;
    }
    for (i = 0; i < GUI_MAX_WINS; i++) {
        if (GuiWindowKind(i) == GUI_WIN_SETTINGS) {
            GuiRaiseToFront(i);
            return 1;
        }
    }
    return 0;
}

void HitClear(void) {
    gSetHitCount = 0;
}

void HitAdd(UINT32 X, UINT32 Y, UINT32 W, UINT32 H, int Kind, int Index) {
    if (gSetHitCount >= SETTINGS_HIT_MAX || W == 0 || H == 0) {
        return;
    }
    gSetHits[gSetHitCount].X = X;
    gSetHits[gSetHitCount].Y = Y;
    gSetHits[gSetHitCount].W = W;
    gSetHits[gSetHitCount].H = H;
    gSetHits[gSetHitCount].Kind = Kind;
    gSetHits[gSetHitCount].Index = Index;
    gSetHitCount++;
}

void ClampItemScroll(void) {
    int N = ItemCount();

    if (gItemSel < 0) {
        gItemSel = 0;
    }
    if (N > 0 && gItemSel >= N) {
        gItemSel = N - 1;
    }
    if (gSetListVisible < 1) {
        gSetListVisible = 1;
    }
    if (gItemSel < gItemScroll) {
        gItemScroll = gItemSel;
    }
    if (gItemSel >= gItemScroll + gSetListVisible) {
        gItemScroll = gItemSel - gSetListVisible + 1;
    }
    if (gItemScroll < 0) {
        gItemScroll = 0;
    }
}

void SelectCategory(SETTINGS_CAT C) {
    if (C < 0 || C >= SETTINGS_CAT_COUNT) {
        return;
    }
    gCat = C;
    gItemSel = CurrentItemIndex();
    gItemScroll = 0;
    ClampItemScroll();
}

int SettingsUiIsFocused(void) {
    return GuiFocusKind() == GUI_WIN_SETTINGS;
}

void SettingsUiRepaint(void) {
    if (!SettingsUiIsFocused()) {
        return;
    }
    PaintMenu();
    GuiBackupFocusWindow();
}

void SettingsUiPaintFocused(void) {
    if (!SettingsUiIsFocused()) {
        return;
    }
    PaintMenu();
}

void SettingsUiOpen(void) {
    gCat = SETTINGS_CAT_DESKTOP;
    gItemSel = CurrentItemIndex();
    gItemScroll = 0;
    gDisplayHint = 0;
    gSetHoverKind = -1;
    gSetHoverIdx = -1;
    gSetPressKind = -1;
    gSetPressIdx = -1;
    PaintMenu();
    DebugWrite("settings: three-pane open\n");
}

void SettingsUiRefresh(void) {
    int i;

    for (i = 0; i < GUI_MAX_WINS; i++) {
        if (GuiWindowKind(i) != GUI_WIN_SETTINGS) {
            continue;
        }
        GuiRaiseToFront(i);
        GuiPaintWindow(i);
        PaintMenu();
        return;
    }
}
