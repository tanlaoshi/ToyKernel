/*
 * FilesUiListDetail.c — PaintList 右栏预览（PR-F-filesui-1）
 *
 * 从 FilesUiList.c 抽出；与 FilesUiPreview.c（内容装载）分工。
 */
#include "FilesUiPrivate.h"

void PaintListDrawPreview(UINT32 Cx, UINT32 Y, UINT32 H, UINT32 LineH, UINT32 Cw) {
    UINT32 ListW;
    UINT32 Px;
    UINT32 Py;
    UINT32 Pw;
    UINT32 InnerX;
    UINT32 InnerY;
    UINT32 InnerW;
    UINT32 InnerH;
    UINT32 CurY;
    UINTN ti;

    if (gPrevW == 0) {
        return;
    }

    ListW = Cw - gPrevW;
    Px = Cx + ListW;
    Py = Y + 8;
    Pw = gPrevW;

    gPrevX = Px;
    HalVideoFillRect(Px, Y, 2, H, ThemePanelSeparator());
    HalVideoFillRect(Px + 2, Y, Pw > 2 ? Pw - 2 : Pw, H, ThemePanelDetailBackground());
    DrawLine(Px + 10, Py, LocStr(MSG_FILES_PREVIEW), ThemeText());
    DrawLine(Px + 10, Py + LineH, gViewTitle[0] ? gViewTitle : LocStr(MSG_FILES_NONE),
             ThemeTextMuted());

    InnerX = Px + 8;
    InnerY = Py + LineH * 2 + 8;
    InnerW = Pw > 16 ? Pw - 16 : Pw;
    InnerH = (Y + H > InnerY + 8) ? (Y + H - InnerY - 8) : 0;

    if (gPrevKind == PREV_EMPTY || gPrevKind == PREV_NONE) {
        if (InnerW > 8 && InnerH > LineH + 8) {
            UiFillRectangle(InnerX, InnerY, InnerW, InnerH > LineH * 4 ? LineH * 4 : InnerH,
                            ThemeDialogFace());
            UiDrawRectangle(InnerX, InnerY,
                            InnerW, InnerH > LineH * 4 ? LineH * 4 : InnerH,
                            ThemePanelSeparator());
            DrawLine(InnerX + 10, InnerY + LineH,
                     LocStr(MSG_FILES_EMPTY), ThemeTextMuted());
        }
    } else if (gPrevKind == PREV_DIR) {
        DrawLine(InnerX, InnerY, LocStr(MSG_FILES_DIR), ThemeTextAccent());
        DrawLine(InnerX, InnerY + LineH, LocStr(MSG_FILES_ENTER_OPEN), ThemeTextMuted());
    } else if (gPrevKind == PREV_ELF) {
        DrawLine(InnerX, InnerY, LocStr(MSG_FILES_ELF), ThemeTextAccent());
        DrawLine(InnerX, InnerY + LineH, LocStr(MSG_FILES_ENTER_RUN), ThemeTextMuted());
    } else if (gPrevKind == PREV_ERR) {
        DrawLine(InnerX, InnerY, LocStr(MSG_FILES_CANT_READ), ThemeTextAccent());
    } else if (gPrevKind == PREV_BIN) {
        DrawLine(InnerX, InnerY, LocStr(MSG_FILES_BINARY), ThemeTextAccent());
        DrawLine(InnerX, InnerY + LineH, LocStr(MSG_FILES_HEX), ThemeTextMuted());
    } else if (gPrevKind == PREV_TEXT && InnerH > LineH) {
        char Row[72];
        int Col = 0;
        UINT32 MaxCols = (InnerW > 8) ? (InnerW - 4) / (FontAdvanceX() ? FontAdvanceX() : 8) : 20;

        if (MaxCols > sizeof(Row) - 1) {
            MaxCols = sizeof(Row) - 1;
        }
        if (MaxCols < 8) {
            MaxCols = 8;
        }
        CurY = InnerY;
        for (ti = 0; ti < gViewLen && CurY + LineH <= InnerY + InnerH; ti++) {
            char C = gView[ti];
            if (C == '\n' || Col >= (int)MaxCols) {
                Row[Col] = 0;
                DrawLine(InnerX, CurY, Row, ThemeText());
                CurY += LineH;
                Col = 0;
                if (C == '\n') {
                    continue;
                }
            }
            if (C == '\r') {
                continue;
            }
            if (C >= 32 && C < 127) {
                Row[Col++] = C;
            } else {
                Row[Col++] = '.';
            }
        }
        if (Col > 0 && CurY + LineH <= InnerY + InnerH) {
            Row[Col] = 0;
            DrawLine(InnerX, CurY, Row, ThemeText());
        }
    }
}
