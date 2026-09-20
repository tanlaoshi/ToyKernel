/*
 * DevicesUi.c — 设备管理器开窗 / 焦点 / 点击（PR-DEV-6）
 */
#include "DevicesUiPrivate.h"

int gDevUiSel;
int gDevUiScroll;
int gDevUiCount;
int gDevUiFilt;
int gDevUiMap[DEVUI_MAP_MAX];
int gDevUiFiltCount;
UINT32 gDevUiListX;
UINT32 gDevUiListY;
UINT32 gDevUiListW;
UINT32 gDevUiListH;
int gDevUiVisible;
UINT32 gDevUiSideX;
UINT32 gDevUiSideW;
UINT32 gDevUiSideRow0;
UINT32 gDevUiSideLineH;
UINT32 gDevUiPrevX;
UINT32 gDevUiPrevW;

int DevicesUiIsFocused(void) {
    return GuiFocusKind() == GUI_WIN_DEVICES;
}

void DevicesUiPaintFocused(void) {
    if (!DevicesUiIsFocused()) {
        return;
    }
    DevicesUiPaint();
}

void DevicesUiRepaint(void) {
    if (!DevicesUiIsFocused()) {
        return;
    }
    DevicesUiPaint();
}

void DevicesUiOpen(void) {
    gDevUiSel = 0;
    gDevUiScroll = 0;
    gDevUiFilt = DEVUI_FILT_ALL;
    DevicesUiReload();
    DevicesUiPaint();
}

void DevicesUiOnClick(UINT32 X, UINT32 Y) {
    int Row;
    int Fi;
    int Idx;
    UINT32 LineH;

    if (!DevicesUiIsFocused()) {
        return;
    }

    if (gDevUiSideW > 0 && X >= gDevUiSideX &&
        X < gDevUiSideX + gDevUiSideW && Y >= gDevUiSideRow0) {
        LineH = gDevUiSideLineH ? gDevUiSideLineH : DEVUI_ROW_H;
        Row = (int)((Y - gDevUiSideRow0) / LineH);
        if (Row >= 0 && Row < DEVUI_FILT_N) {
            if (gDevUiFilt != Row) {
                gDevUiFilt = Row;
                gDevUiScroll = 0;
                DevicesUiRebuildFilt();
                DevicesUiRepaint();
            }
        }
        return;
    }

    if (gDevUiPrevW > 0 && X >= gDevUiPrevX) {
        return;
    }
    if (Y < gDevUiListY || Y >= gDevUiListY + gDevUiListH) {
        return;
    }
    if (X < gDevUiListX || X >= gDevUiListX + gDevUiListW) {
        return;
    }
    if (gDevUiVisible <= 0 || DEVUI_ROW_H == 0) {
        return;
    }
    Row = (int)((Y - gDevUiListY) / DEVUI_ROW_H);
    Fi = gDevUiScroll + Row;
    if (Fi < 0 || Fi >= gDevUiFiltCount) {
        return;
    }
    Idx = gDevUiMap[Fi];
    if (Idx < 0 || Idx >= gDevUiCount) {
        return;
    }
    if (gDevUiSel != Idx) {
        gDevUiSel = Idx;
        DevicesUiRepaint();
    }
}
