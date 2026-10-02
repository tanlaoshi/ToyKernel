/*
 * TtyUiPaint.c — 会话缓冲绘制
 */
#include "TtyUiPrivate.h"

static void DrawLine(UINT32 X, UINT32 Y, const char *S, UINT32 Fg) {
    if (S) {
        HalVideoDrawStringAt(X, Y, S, Fg);
    }
}

void TtyPaint(void) {
    UINT32 X;
    UINT32 Y;
    UINT32 W;
    UINT32 H;
    UINT32 Bg;
    UINT32 LineH;
    UINT32 Ax;
    UINT32 MaxCols;
    UINT32 VisLines;
    UINT32 TextTop;
    UINT32 TextBot;
    UINTN i;
    UINTN Line;
    UINTN Col;
    UINTN StartLine;
    char Row[128];
    char Head[TTY_STATUS_MAX + 32];
    UINTN Hn;
    const char *P;
    UINT32 Fg;

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

    Hn = 0;
    P = "TTY  ";
    while (*P && Hn + 1 < sizeof(Head)) {
        Head[Hn++] = *P++;
    }
    P = gTtyStatus;
    while (P && *P && Hn + 1 < sizeof(Head)) {
        Head[Hn++] = *P++;
    }
    Head[Hn] = 0;
    DrawLine(X + 8, Y + 6, Head, ThemeShellPrompt());

    TextTop = Y + 6 + LineH + 4;
    TextBot = Y + H - 8;
    if (TextBot <= TextTop + LineH) {
        TextBot = TextTop + LineH;
    }
    VisLines = (TextBot - TextTop) / LineH;
    if (VisLines < 1) {
        VisLines = 1;
    }
    MaxCols = (W > 16) ? ((W - 16) / Ax) : 40;
    if (MaxCols >= sizeof(Row)) {
        MaxCols = sizeof(Row) - 1;
    }
    if (MaxCols < 8) {
        MaxCols = 8;
    }
    TtyClampScroll(VisLines);
    StartLine = (UINTN)gTtyScroll;

    Line = 0;
    Col = 0;
    for (i = 0; i < gTtyLen; i++) {
        char C = gTtyBuf[i];

        if (C == '\n' || Col >= MaxCols) {
            Row[Col] = 0;
            if (Line >= StartLine && Line < StartLine + VisLines) {
                UINT32 Dy = TextTop + (UINT32)(Line - StartLine) * LineH;
                /* 输入行（tty>）用提示符色，其余正文色 */
                Fg = ThemeShellText();
                if (Col >= 4 && Row[0] == 't' && Row[1] == 't' && Row[2] == 'y' &&
                    Row[3] == '>') {
                    Fg = ThemeShellPrompt();
                }
                DrawLine(X + 8, Dy, Row, Fg);
            }
            Line++;
            Col = 0;
            if (C == '\n') {
                continue;
            }
        }
        Row[Col++] = C;
    }
    Row[Col] = 0;
    if (Col > 0 || gTtyLen == 0) {
        if (Line >= StartLine && Line < StartLine + VisLines) {
            UINT32 Dy = TextTop + (UINT32)(Line - StartLine) * LineH;
            Fg = ThemeShellText();
            if (Col >= 4 && Row[0] == 't' && Row[1] == 't' && Row[2] == 'y' &&
                Row[3] == '>') {
                Fg = ThemeShellPrompt();
            }
            DrawLine(X + 8, Dy, Row, Fg);
        }
    }

    HalVideoClearClip();
    GuiBackupFocusWindow();
    GuiFrameBufferEnd();
}

void TtyUiPaintFocused(void) {
    TtyPaint();
}

/* 勿 GuiRaiseToFront：会 Sync 旧备份盖字，且 Raise→Repaint 递归 */
void TtyUiRepaint(void) {
    if (!TtyUiIsFocused()) {
        return;
    }
    TtyPaint();
    /* ShellTask 每轮也会 Present；这里立即刷，打字才看得见 */
    if (!GuiPresentBlocked()) {
        HalVideoPresent();
    }
}
