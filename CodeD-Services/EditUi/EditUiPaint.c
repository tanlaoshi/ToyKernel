/*
 * EditUiPaint.c — 客户区、光标、Save 按钮
 * 核心：EditUi.c
 */
#include "EditUiPrivate.h"

static void DrawLine(UINT32 X, UINT32 Y, const char *S, UINT32 Fg) {
    if (!S) {
        return;
    }
    HalVideoDrawStringAt(X, Y, S, Fg);
}

static void Paint(void) {
    UINT32 X;
    UINT32 Y;
    UINT32 W;
    UINT32 H;
    UINT32 Bg;
    UINT32 LineH;
    UINT32 Ax;
    UINT32 Fw;
    UINT32 MaxCols;
    UINT32 VisLines;
    UINTN i;
    UINTN Line;
    UINTN Col;
    UINTN StartLine;
    char Row[96];
    char Head[100];
    UINT32 TextTop;
    UINT32 TextBot;
    UINT32 CurY;

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
    Ax = FontAdvanceX();
    if (Ax < 8) {
        Ax = 8;
    }

    Head[0] = 0;
    CopyStr(Head, sizeof(Head), gPath[0] ? gPath : "(no path)");
    DrawLine(X + 8, Y + 6, Head, ThemeText());
    DrawLine(X + 8, Y + 6 + LineH,
             gEditDirty ? "* dirty   Esc=hint  Ctrl+S=save" : "  clean   Esc=hint  Ctrl+S=save",
             ThemeTextMuted());

    gEditSave.Button.W = 72;
    gEditSave.Button.H = LineH + 8;
    gEditSave.Button.X = X + W - gEditSave.Button.W - 12;
    gEditSave.Button.Y = Y + 4;
    gEditSave.Button.Text = "Save";
    gEditSave.Button.Enabled = (gPath[0] != 0);
    if (gEditSave.Button.X > X + 8) {
        gEditSave.Button.Visible = 1;
        UiButtonDraw(&gEditSave.Button);
    } else {
        gEditSave.Button.Visible = 0;
    }

    TextTop = Y + 6 + LineH * 2 + 4;
    TextBot = Y + H - LineH - 8;
    if (TextBot <= TextTop + LineH) {
        TextBot = TextTop + LineH;
    }
    VisLines = (TextBot - TextTop) / LineH;
    if (VisLines < 1) {
        VisLines = 1;
    }
    EditClampScroll(VisLines);

    MaxCols = (W > 24) ? (W - 24) / Ax : 40;
    if (MaxCols > sizeof(Row) - 1) {
        MaxCols = sizeof(Row) - 1;
    }
    if (MaxCols < 8) {
        MaxCols = 8;
    }

    StartLine = (UINTN)gScrollLine;
    Line = 0;
    Col = 0;
    CurY = TextTop;
    Fw = 0;
    for (i = 0; i <= gEditLen && CurY + LineH <= TextBot; i++) {
        int AtCursor = (i == gCursor);
        char C = (i < gEditLen) ? gBuf[i] : 0;

        if (Line < StartLine) {
            if (i < gEditLen && C == '\n') {
                Line++;
                Col = 0;
            } else if (i < gEditLen) {
                Col++;
            }
            continue;
        }
        if (Line > StartLine + VisLines) {
            break;
        }

        if (AtCursor && Col < MaxCols) {
            HalVideoFillRect(X + 8 + (UINT32)Col * Ax, CurY, Ax, LineH, ThemeTextMuted());
        }

        if (i >= gEditLen) {
            break;
        }
        if (C == '\n' || Col >= MaxCols) {
            Row[Col] = 0;
            DrawLine(X + 8, CurY, Row, ThemeText());
            CurY += LineH;
            Line++;
            Col = 0;
            Fw = 0;
            if (C == '\n') {
                continue;
            }
        }
        if (C == '\r') {
            continue;
        }
        if (C >= 32 && C < 127) {
            Row[Col++] = C;
            Fw = 1;
        } else {
            Row[Col++] = '.';
            Fw = 1;
        }
    }
    if (Fw && Col > 0 && CurY + LineH <= TextBot) {
        Row[Col] = 0;
        DrawLine(X + 8, CurY, Row, ThemeText());
    }

    DrawLine(X + 8, Y + H - LineH - 4,
             gEditStatus[0] ? gEditStatus : " ", ThemeTextMuted());

    HalVideoClearClip();
    GuiBackupFocusWindow();
    GuiFrameBufferEnd();
}

void EditUiRepaint(void) {
    if (!EditUiIsFocused()) {
        return;
    }
    Paint();
}

void EditUiPaintFocused(void) {
    Paint();
}
