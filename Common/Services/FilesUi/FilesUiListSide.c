/*
 * FilesUiListSide.c — PaintList 左栏卷列表（PR-F-filesui-1）
 *
 * 从 FilesUiList.c 抽出；不改绘制语义。
 */
#include "FilesUiPrivate.h"

void PaintListDrawSide(UINT32 X, UINT32 Y, UINT32 H, UINT32 LineH, UINT32 SideW) {
    UINT32 RowW;
    int i;

    if (SideW == 0) {
        return;
    }

    gSideX = X;
    gSideY = Y;
    gSideW = SideW;
    gSideLineH = LineH;
    gSideRow0 = Y + 8 + LineH + 4;
    RowW = SideW > 10 ? SideW - 10 : SideW;

    HalVideoFillRect(X, Y, SideW, H, ThemePanelSideBackground());
    if (SideW > 3) {
        HalVideoFillRect(X + SideW - 3, Y, 3, H, ThemePanelSeparator());
    }
    DrawLine(X + 8, Y + 8, LocStr(MSG_FILES_VOLUMES), ThemeText());
    for (i = 0; i < gPlaceCount; i++) {
        UiDrawListRow(X + 4, gSideRow0 + (UINT32)i * LineH, RowW, LineH,
                      gPlaces[i].Label,
                      i == gSideSel, i == gSideHover);
    }
}
