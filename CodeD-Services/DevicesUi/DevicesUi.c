/*
 * DevicesUi.c — 设备管理器开窗 / 焦点 / 点击（PR-DEV-6）
 */
#include "DevicesUiPrivate.h"
#include "Debug.h"

int gDevUiSel;
int gDevUiScroll;
int gDevUiCount;
int gDevUiFilt;
int gDevUiSummary;
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
    gDevUiSummary = 1;
    DevicesUiReload();
    DevicesUiPaint();
#if TOY_KERNEL_DEBUG
    /* PR-DEV-ui-summary-data：开窗时串口自检摘要字段非空 */
    DevicesUiSummarySelfCheck();
#endif
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
        if (Row == 0) {
            /* 摘要页（置顶项；非筛选器） */
            if (!gDevUiSummary) {
                gDevUiSummary = 1;
                DevicesUiRepaint();
            }
        } else if (Row >= 1 && Row < DEVUI_FILT_N + 1) {
            /* 筛选行 1..N → FILT 0..N-1 */
            int NewFilt = Row - 1;
            if (gDevUiSummary || gDevUiFilt != NewFilt) {
                gDevUiSummary = 0;
                gDevUiFilt = NewFilt;
                gDevUiScroll = 0;
                DevicesUiRebuildFilt();
                DevicesUiRepaint();
            }
        }
        return;
    }

    /* 摘要页只读：内容区点击不选设备 */
    if (gDevUiSummary) {
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
