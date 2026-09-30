/*
 * FilesUiListRows.c — PaintList 路径头 + 文件行（PR-F-filesui-1）
 *
 * 从 FilesUiList.c 抽出；行绘制 / 选区滚动与空目录提示。
 */
#include "FilesUiPrivate.h"

void PaintListDrawHeader(UINT32 Cx, UINT32 Y, UINT32 LineH, UINT32 Cw) {
    UINT32 ListW;
    UINT32 HintMax;
    const char *Hint1 = "Enter open  d/Del delete";
    const char *Hint2 = "n mkdir  f newfile  r rename";
    char PathShow[FILES_PATH_MAX + 8];
    int n;

    /* 先算预览宽，再画 Path/快捷键（避免右栏盖住 mkdir 等） */
    gPrevW = 0;
    gPrevX = Cx;
    if (Cw > 360u) {
        gPrevW = Cw * 2u / 5u;
        if (gPrevW < 160u) {
            gPrevW = 160u;
        }
        if (gPrevW + 120u > Cw) {
            gPrevW = Cw > 120u ? Cw - 120u : 0;
        }
    }

    ListW = Cw - gPrevW;
    PathShow[0] = 0;
    CopyStr(PathShow, sizeof(PathShow), "Path: ");
    n = 0;
    while (PathShow[n]) {
        n++;
    }
    if (gCwd[0]) {
        CopyStr(PathShow + n, (int)sizeof(PathShow) - n, gCwd);
    } else {
        CopyStr(PathShow + n, (int)sizeof(PathShow) - n, "/");
    }
    HintMax = ListW > 16 ? ListW - 16 : ListW;
    (void)HintMax;
    DrawLine(Cx + 8, Y + 8, PathShow, ThemeText());
    DrawLine(Cx + 8, Y + 8 + LineH, Hint1, ThemeTextMuted());
    DrawLine(Cx + 8, Y + 8 + LineH * 2, Hint2, ThemeTextMuted());
    if (gStatus[0]) {
        DrawLine(Cx + 8, Y + 8 + LineH * 3, gStatus, ThemeTextAccent());
    }
}

void PaintListDrawRows(UINT32 ListX, UINT32 ListW, UINT32 Y, UINT32 H, UINT32 LineH) {
    UINT32 HeadLines;
    UINT32 RowY;
    int Visible;
    int i;
    char Line[96];

    HeadLines = gStatus[0] ? 4u : 3u;
    Visible = 0;
    if (H > 8 + LineH * (HeadLines + 1)) {
        Visible = (int)((H - 8 - LineH * (HeadLines + 1)) / LineH);
    }
    if (Visible < 1) {
        Visible = 1;
    }
    if (gSelected < gScroll) {
        gScroll = gSelected;
    }
    if (gSelected >= gScroll + Visible) {
        gScroll = gSelected - Visible + 1;
    }
    if (gScroll < 0) {
        gScroll = 0;
    }

    gListVisible = Visible;
    gListTop = Y + 8 + LineH * HeadLines + 4;
    gListLineH = LineH;
    gSbVisible = (gCount > Visible) ? 1 : 0;
    gSbW = FILES_SB_W;
    gSbH = (UINT32)Visible * LineH;
    if (gSbH + gListTop > Y + H) {
        gSbH = (Y + H > gListTop) ? (Y + H - gListTop) : 0;
    }
    gSbX = (ListW > FILES_SB_W + 8) ? (ListX + ListW - FILES_SB_W - 4) : (ListX + 4);
    if (gPrevW > 0 && gSbX + gSbW > ListX + ListW) {
        gSbX = ListX + 4;
    }
    gSbY = gListTop;
    gListRowW = ListW > 8 ? ListW - 8 : ListW;
    if (gSbVisible && gListRowW > FILES_SB_W + 8) {
        gListRowW -= (FILES_SB_W + 4);
    }

    RowY = gListTop;
    for (i = 0; i < Visible && gScroll + i < gCount; i++) {
        const FAT_DIRECTORY_ENTRY *E = &gEnts[gScroll + i];
        int Idx = gScroll + i;
        int k = 0;
        int j;

        if (E->Attr & FAT_ATTR_DIR) {
            Line[k++] = '[';
            Line[k++] = 'D';
            Line[k++] = ']';
            Line[k++] = ' ';
        } else {
            Line[k++] = ' ';
            Line[k++] = ' ';
            Line[k++] = ' ';
            Line[k++] = ' ';
        }
        for (j = 0; E->Name[j] && k < (int)sizeof(Line) - 1; j++) {
            Line[k++] = E->Name[j];
        }
        Line[k] = 0;
        UiDrawListRow(ListX + 4, RowY, gListRowW, LineH, Line,
                      Idx == gSelected, Idx == gHoverIdx);
        RowY += LineH;
    }
    if (gSbVisible && gSbH > 0) {
        UiDrawScrollBar(gSbX, gSbY, gSbW, gSbH, gScroll, Visible, gCount);
    }
    if (gCount == 0) {
        UINT32 BoxX = ListX + 12;
        UINT32 BoxY = gListTop;
        UINT32 BoxW = ListW > 24 ? ListW - 24 : ListW;
        UINT32 BoxH = LineH * 4 + 20;
        UINT32 Remain;

        if (BoxY + 8 < Y + H) {
            Remain = (Y + H) - BoxY - 8;
            if (BoxH > Remain) {
                BoxH = Remain;
            }
        }
        if (BoxW > 8 && BoxH > LineH + 8) {
            UiFillRectangle(BoxX, BoxY, BoxW, BoxH, ThemeDialogFace());
            UiDrawRectangle(BoxX, BoxY, BoxW, BoxH, ThemeDialogBorder());
            DrawLine(BoxX + 12, BoxY + LineH, LocStr(MSG_FILES_EMPTY), ThemeTextMuted());
            if (BoxH >= LineH * 3) {
                DrawLine(BoxX + 12, BoxY + LineH * 2 + 4,
                         LocStr(MSG_FILES_EMPTY_HINT), ThemeTextMuted());
            }
        }
    }
}
