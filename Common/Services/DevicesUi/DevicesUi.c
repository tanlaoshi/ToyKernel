/*
 * DevicesUi.c — 设备管理器开窗 / 焦点（PR-DEV-6）
 */
#include "DevicesUiPrivate.h"

int gDevUiSel;
int gDevUiScroll;
int gDevUiCount;
UINT32 gDevUiListX;
UINT32 gDevUiListY;
UINT32 gDevUiListW;
UINT32 gDevUiListH;
int gDevUiVisible;

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
    GuiBackupFocusWindow();
}

void DevicesUiOpen(void) {
    gDevUiSel = 0;
    gDevUiScroll = 0;
    DevicesUiReload();
    DevicesUiPaint();
}

void DevicesUiOnClick(UINT32 X, UINT32 Y) {
    int Row;
    int Idx;

    if (!DevicesUiIsFocused()) {
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
    Idx = gDevUiScroll + Row;
    if (Idx < 0 || Idx >= gDevUiCount) {
        return;
    }
    gDevUiSel = Idx;
    DevicesUiRepaint();
}
