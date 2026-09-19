/*
 * FilesUiPaint.c — Files 绘制（PR-S-filesui-split-1）
 *
 * 从 FilesUi.c 迁出 Paint*；只搬家、不改逻辑。
 */
#include "FilesUiPriv.h"

void PaintOverlay(const char *Line1, const char *Line2, const char *Line3) {
    UINT32 X;
    UINT32 Y;
    UINT32 W;
    UINT32 H;
    UINT32 Bg;
    UINT32 LineH;
    UINT32 BoxX;
    UINT32 BoxY;
    UINT32 BoxW;
    UINT32 BoxH;

    if (!GuiFocusClient(&X, &Y, &W, &H, &Bg)) {
        return;
    }
    LineH = FontAdvanceY();
    if (LineH < 16) {
        LineH = 16;
    }
    BoxW = W > 40 ? W - 40 : W;
    BoxH = LineH * 5 + 24;
    if (BoxH > H - 20) {
        BoxH = H > 20 ? H - 20 : H;
    }
    BoxX = X + (W - BoxW) / 2;
    BoxY = Y + (H - BoxH) / 2;

    GuiFrameBufferBegin();
    HalVideoFillRect(BoxX, BoxY, BoxW, BoxH, COLOR_LIGHT_GRAY);
    /* PR-G11：确认/输入框边框走 UiDrawRectangle */
    UiDrawRectangle(BoxX, BoxY, BoxW, BoxH, COLOR_BLACK);
    if (BoxW > 4 && BoxH > 4) {
        UiDrawRectangle(BoxX + 1, BoxY + 1, BoxW - 2, BoxH - 2, COLOR_DARK_GRAY);
    }
    HalVideoSetClipRegion(BoxX + 4, BoxY + 4, BoxW > 8 ? BoxW - 8 : BoxW, BoxH > 8 ? BoxH - 8 : BoxH,
                          COLOR_LIGHT_GRAY);
    DrawLine(BoxX + 12, BoxY + 12, Line1 ? Line1 : "", COLOR_BLACK);
    if (Line2) {
        DrawLine(BoxX + 12, BoxY + 12 + LineH, Line2, COLOR_BLACK);
    }
    if (Line3) {
        DrawLine(BoxX + 12, BoxY + 12 + LineH * 2, Line3, COLOR_DARK_GRAY);
    }
    HalVideoClearClip();
    GuiBackupFocusWindow();
    GuiFrameBufferEnd();
}

void DrawLine(UINT32 X, UINT32 Y, const char *S, UINT32 Fg) {
    if (!S) {
        return;
    }
    HalVideoDrawStringAt(X, Y, S, Fg);
}

void PaintView(void) {
    UINT32 X;
    UINT32 Y;
    UINT32 W;
    UINT32 H;
    UINT32 Bg;
    UINT32 LineH;
    UINT32 CurY;
    UINTN i;
    char Title[80];
    int ti;

    if (!GuiFocusClient(&X, &Y, &W, &H, &Bg)) {
        return;
    }
    GuiFrameBufferBegin();
    HalVideoFillRect(X, Y, W, H, Bg);
    HalVideoSetClipRegion(X, Y, W, H, Bg);

    LineH = FontAdvanceY();
    if (LineH < 16) {
        LineH = 16;
    }

    Title[0] = 0;
    CopyStr(Title, sizeof(Title), LocStr(MSG_FILES_VIEW));
    ti = 0;
    while (Title[ti]) {
        ti++;
    }
    CopyStr(Title + ti, (int)sizeof(Title) - ti, gViewTitle);
    DrawLine(X + 8, Y + 8, Title, COLOR_BLACK);
    DrawLine(X + 8, Y + 8 + LineH, "Esc = back to list", COLOR_DARK_GRAY);

    CurY = Y + 8 + LineH * 2 + 4;
    {
        char Row[96];
        int Col = 0;
        UINT32 MaxCols = (W > 16) ? (W - 16) / (FontAdvanceX() ? FontAdvanceX() : 8) : 40;

        if (MaxCols > sizeof(Row) - 1) {
            MaxCols = sizeof(Row) - 1;
        }
        if (MaxCols < 8) {
            MaxCols = 8;
        }
        for (i = 0; i < gViewLen && CurY + LineH <= Y + H; i++) {
            char C = gView[i];
            if (C == '\n' || Col >= (int)MaxCols) {
                Row[Col] = 0;
                DrawLine(X + 8, CurY, Row, COLOR_BLACK);
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
        if (Col > 0 && CurY + LineH <= Y + H) {
            Row[Col] = 0;
            DrawLine(X + 8, CurY, Row, COLOR_BLACK);
        }
    }

    HalVideoClearClip();
    GuiBackupFocusWindow();
    GuiFrameBufferEnd();
}

void PaintConfirm(void) {
    char Line2[80];
    const char *Name = "?";

    if (gSelected >= 0 && gSelected < gCount) {
        Name = gEnts[gSelected].Name;
    }
    CopyStr(Line2, sizeof(Line2), "Delete ");
    {
        int n = 0;
        while (Line2[n]) {
            n++;
        }
        CopyStr(Line2 + n, (int)sizeof(Line2) - n, Name);
        n = 0;
        while (Line2[n]) {
            n++;
        }
        CopyStr(Line2 + n, (int)sizeof(Line2) - n, " ?");
    }
    PaintList();
    PaintOverlay("Confirm delete", Line2, "Y = yes   N/Esc = cancel");
}

void PaintPrompt(void) {
    char Title[40];
    char Line2[FILES_NAME_MAX + 8];
    int i;

    if (gPromptKind == FILES_PROMPT_MKDIR) {
        CopyStr(Title, sizeof(Title), LocStr(MSG_FILES_NEW_DIR));
    } else if (gPromptKind == FILES_PROMPT_NEWFILE) {
        CopyStr(Title, sizeof(Title), LocStr(MSG_FILES_NEW_FILE));
    } else {
        CopyStr(Title, sizeof(Title), LocStr(MSG_FILES_RENAME));
    }
    Line2[0] = '>';
    Line2[1] = ' ';
    for (i = 0; i < gPromptLen && i < FILES_NAME_MAX - 1; i++) {
        Line2[2 + i] = gPrompt[i];
    }
    Line2[2 + i] = '_';
    Line2[3 + i] = 0;
    PaintList();
    PaintOverlay(Title, Line2, LocStr(MSG_FILES_PROMPT_HINT));
}

void Paint(void) {
    if (!FocusFilesWindow()) {
        return;
    }
    if (gMode == FILES_MODE_VIEW) {
        PaintView();
    } else if (gMode == FILES_MODE_CONFIRM) {
        PaintConfirm();
    } else if (gMode == FILES_MODE_PROMPT) {
        PaintPrompt();
    } else {
        PaintList();
    }
}

