/*
 * FilesUiListSide.c — PaintList 左栏卷列表（PR-F-filesui-1）
 *
 * 从 FilesUiList.c 抽出；不改绘制语义。
 */
#include "FilesUiPrivate.h"

void PaintListDrawSide(UINT32 X, UINT32 Y, UINT32 H, UINT32 LineH, UINT32 SideW) {
    UINT32 RowW;
    UINT32 Pad;
    int i;

    if (SideW == 0) {
        return;
    }

    Pad = UI_LAYOUT_PAD;
    gSideX = X;
    gSideY = Y;
    gSideW = SideW;
    gSideLineH = LineH;
    gSideRow0 = Y + Pad + LineH + UI_LAYOUT_GAP;
    RowW = SideW > Pad * 2u ? SideW - Pad * 2u : SideW;

    HalVideoFillRect(X, Y, SideW, H, ThemePanelSideBackground());
    if (SideW > UI_LAYOUT_SEP_W) {
        HalVideoFillRect(X + SideW - UI_LAYOUT_SEP_W, Y, UI_LAYOUT_SEP_W, H,
                         ThemePanelSeparator());
    }
    DrawLine(X + Pad, Y + Pad, LocStr(MSG_FILES_VOLUMES), ThemeText());
    for (i = 0; i < gPlaceCount; i++) {
        UiDrawListRow(X + Pad, gSideRow0 + (UINT32)i * LineH, RowW, LineH,
                      gPlaces[i].Label,
                      i == gSideSel, i == gSideHover);
    }
}
