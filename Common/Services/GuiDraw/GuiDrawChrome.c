/*
 * GuiDrawChrome.c — 标题栏、边框、关闭钮与遮挡画线
 * 核心：GuiDraw.c
 */
#include "GuiPrivate.h"
#include "UI.h"
#include "HalVideo.h"
#include "Hal.h"
#include "Theme.h"
#include "Font.h"
#include "Desktop.h"

void CloseButtonRect(const GUI_WINDOW *W, UINT32 *Bx, UINT32 *By,
                            UINT32 *Bw, UINT32 *Bh) {
    *Bw = CLOSE_SIZE;
    *Bh = CLOSE_SIZE;
    *Bx = W->X + W->Width - *Bw - CLOSE_MARGIN;
    *By = W->Y + (TITLE_HEIGHT - *Bh) / 2;
}


/* 更高 z（数组下标更大）的窗口是否盖住该像素 */
int PixelOccludedByAbove(int Idx, UINT32 X, UINT32 Y) {
    int j;

    for (j = Idx + 1; j < MAX_WINS; j++) {
        if (!gWindows[j].Active) {
            continue;
        }
        if (X >= gWindows[j].X && X < gWindows[j].X + gWindows[j].Width &&
            Y >= gWindows[j].Y && Y < gWindows[j].Y + gWindows[j].Height) {
            return 1;
        }
    }
    return 0;
}


void FillRectOccluded(int Idx, UINT32 X, UINT32 Y, UINT32 W, UINT32 H,
                             UINT32 Color) {
    UINT32 Row;
    UINT32 Col;
    UINT32 RunStart;
    int InRun;

    if (!W || !H) {
        return;
    }
    for (Row = 0; Row < H; Row++) {
        UINT32 Py = Y + Row;

        InRun = 0;
        RunStart = 0;
        for (Col = 0; Col < W; Col++) {
            UINT32 Px = X + Col;
            int Occ = PixelOccludedByAbove(Idx, Px, Py);

            if (!Occ && !InRun) {
                RunStart = Col;
                InRun = 1;
            } else if (Occ && InRun) {
                HalVideoFillRect(X + RunStart, Py, Col - RunStart, 1, Color);
                InRun = 0;
            }
        }
        if (InRun) {
            HalVideoFillRect(X + RunStart, Py, W - RunStart, 1, Color);
        }
    }
}

void DrawHLineOccluded(int Idx, UINT32 X0, UINT32 X1, UINT32 Y,
                              UINT32 Color) {
    UINT32 X;
    UINT32 RunStart = 0;
    int InRun = 0;

    if (X1 < X0 || Y >= gScreenHeight) {
        return;
    }
    for (X = X0; X <= X1; X++) {
        int Occ = PixelOccludedByAbove(Idx, X, Y);

        if (!Occ && !InRun) {
            RunStart = X;
            InRun = 1;
        } else if (Occ && InRun) {
            HalVideoFillRect(RunStart, Y, X - RunStart, 1, Color);
            InRun = 0;
        }
    }
    if (InRun) {
        HalVideoFillRect(RunStart, Y, X1 - RunStart + 1, 1, Color);
    }
}


void DrawVLineOccluded(int Idx, UINT32 X, UINT32 Y0, UINT32 Y1,
                              UINT32 Color) {
    UINT32 Y;
    UINT32 RunStart = 0;
    int InRun = 0;

    if (Y1 < Y0 || X >= gScreenWidth) {
        return;
    }
    for (Y = Y0; Y <= Y1; Y++) {
        int Occ = PixelOccludedByAbove(Idx, X, Y);

        if (!Occ && !InRun) {
            RunStart = Y;
            InRun = 1;
        } else if (Occ && InRun) {
            HalVideoFillRect(X, RunStart, 1, Y - RunStart, Color);
            InRun = 0;
        }
    }
    if (InRun) {
        HalVideoFillRect(X, RunStart, 1, Y1 - RunStart + 1, Color);
    }
}


void DrawCloseButton(int Idx, const GUI_WINDOW *W) {
    UINT32 Bx;
    UINT32 By;
    UINT32 Bw;
    UINT32 Bh;
    UINT32 Pad;
    UINT32 I;
    UINT32 Span;
    UINT32 CloseBg = ThemeCloseButton();
    UINT32 Edge = ThemeWindowBorderFocus();

    CloseButtonRect(W, &Bx, &By, &Bw, &Bh);
    FillRectOccluded(Idx, Bx, By, Bw, Bh, CloseBg);
    if (Bw >= 2 && Bh >= 2) {
        DrawHLineOccluded(Idx, Bx, Bx + Bw - 1, By, Edge);
        DrawHLineOccluded(Idx, Bx, Bx + Bw - 1, By + Bh - 1, Edge);
        DrawVLineOccluded(Idx, Bx, By, By + Bh - 1, Edge);
        DrawVLineOccluded(Idx, Bx + Bw - 1, By, By + Bh - 1, Edge);
    }
    /* 字体为 16×32，24×24 按钮内放不下；用对角线画居中 × */
    Pad = 7;
    if (Bw > Pad * 2 + 2 && Bh > Pad * 2 + 2) {
        Span = Bw - 1 - Pad * 2;
        for (I = 0; I <= Span; I++) {
            UINT32 PxA = Bx + Pad + I;
            UINT32 PyA = By + Pad + I;
            UINT32 PxB = Bx + Bw - 1 - Pad - I;
            UINT32 PyB = By + Pad + I;

            if (!PixelOccludedByAbove(Idx, PxA, PyA)) {
                HalVideoDrawPixelRaw(PxA, PyA, ThemeWindowTitleText());
            }
            if (!PixelOccludedByAbove(Idx, PxB, PyB)) {
                HalVideoDrawPixelRaw(PxB, PyB, ThemeWindowTitleText());
            }
        }
    }
}


void RefreshOtherChrome(int SkipIdx) {
    int i;

    for (i = 0; i < MAX_WINS; i++) {
        if (gWindows[i].Active && i != SkipIdx) {
            DrawWindowChromeAt(i);
        }
    }
}

void DrawTitleStringOccluded(int Idx, const GUI_WINDOW *W) {
    const char *S;
    UINT32 X;
    UINT32 Y;

    if (W->Title == 0 || W->Title[0] == 0) {
        return;
    }
    S = W->Title;
    X = W->X + 8;
    Y = W->Y + 4;
    while (*S) {
        UINT32 Cp;
        UINTN N;
        UINT32 Adv;

        N = Utf8Decode(S, &Cp);
        if (N == 0) {
            S++;
            continue;
        }
        Adv = FontCodepointAdvance(Cp);
        if (Adv == 0) {
            Adv = 8;
        }
        if (!PixelOccludedByAbove(Idx, X, Y) &&
            !PixelOccludedByAbove(Idx, X + Adv / 2, Y)) {
            HalVideoDrawCodepointAt(X, Y, Cp, ThemeWindowTitleText());
        }
        X += Adv;
        S += N;
    }
}

/* 仅重绘标题栏与边框，保留客户区已有文字；不画到上层窗口上 */
void DrawWindowChromeAt(int Idx) {
    const GUI_WINDOW *W = &gWindows[Idx];
    UINT32 Border;
    UINT32 Th;
    UINT32 Row;
    int CursorWas;
    int CursorOnTitle;
    int Occlude;

    if (!W->Active) {
        return;
    }
    Border = WindowBorderColor(Idx);
    Th = TITLE_HEIGHT;
    if (Th > W->Height) {
        Th = W->Height;
    }
    HalVideoClearClip();
    /*
     * 标题重画会使光标 save-under 失效；若光标仍显示在标题上，
     * 先擦掉再画，结束时重采，否则点击会留下光标方框。
     */
    CursorWas = gCursorVisible;
    CursorOnTitle = CursorWas && PointInTitle(W, gCursorX, gCursorY);
    if (CursorOnTitle) {
        GfxIrqEnter();
        CursorRestore();
        GfxIrqLeave();
    }
    /*
     * 顶层窗必须 Occlude=0：备份重叠区常烙有下层标题灰块，
     * Occlude 会跳过不重画 → 透视。
     */
    Occlude = WindowOccludedByOther(Idx) ? 1 : 0;
    for (Row = 0; Row < Th; Row++) {
        UINT32 Color = TitleBarColorAtRow(Idx, Row, Th);

        if (Occlude) {
            FillRectOccluded(Idx, W->X, W->Y + Row, W->Width, 1, Color);
        } else {
            HalVideoFillRect(W->X, W->Y + Row, W->Width, 1, Color);
        }
    }
    if (Occlude) {
        DrawHLineOccluded(Idx, W->X, W->X + W->Width - 1, W->Y, Border);
        DrawHLineOccluded(Idx, W->X, W->X + W->Width - 1, W->Y + W->Height - 1,
                          Border);
        DrawVLineOccluded(Idx, W->X, W->Y, W->Y + W->Height - 1, Border);
        DrawVLineOccluded(Idx, W->X + W->Width - 1, W->Y, W->Y + W->Height - 1,
                          Border);
        DrawTitleStringOccluded(Idx, W);
        DrawCloseButton(Idx, W);
    } else {
        HalVideoFillRect(W->X, W->Y, W->Width, 1, Border);
        HalVideoFillRect(W->X, W->Y + W->Height - 1, W->Width, 1, Border);
        HalVideoFillRect(W->X, W->Y, 1, W->Height, Border);
        HalVideoFillRect(W->X + W->Width - 1, W->Y, 1, W->Height, Border);
        if (W->Title != 0 && W->Title[0] != 0) {
            HalVideoDrawStringAt(W->X + 8, W->Y + 4, W->Title, ThemeWindowTitleText());
        }
        DrawCloseButton(Idx, W);
    }
    if (CursorOnTitle) {
        GfxIrqEnter();
        CursorPaint();
        GfxIrqLeave();
    }
}
