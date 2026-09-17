/*
 * GuiDraw.c — 窗绘制 / chrome / 遮挡（PR-S-compose-split-1）
 *
 * 从 GuiCompose.c 迁出；只搬家、不改逻辑。
 */
#include "GuiPriv.h"
#include "UI.h"
#include "HalVideo.h"
#include "Hal.h"
#include "Theme.h"
#include "Font.h"
#include "Desktop.h"

UINT32 TitleBarColor(int Idx) {
    if (!gWindows[Idx].Active) {
        return ThemeWindowTitleIdle();
    }
    if (Idx == gFocusWin) {
        return ThemeWindowTitleFocus();
    }
    if (Idx == gHoverWin) {
        return ThemeWindowTitleHover();
    }
    return ThemeWindowTitleIdle();
}

/* 通道逐行插值：Row=0 → Top，Row=TitleH-1 → Bottom（PR-GUI-l2-gradient） */
UINT32 TitleBarColorAtRow(int Idx, UINT32 Row, UINT32 TitleH) {
    UINT32 Top;
    UINT32 Bot;
    UINT8 T;

    Top = TitleBarColor(Idx);
    if (TitleH <= 1u) {
        return Top;
    }
    if (Row >= TitleH) {
        Row = TitleH - 1u;
    }
    Bot = ThemeWindowTitleGradientBottom(Top);
    T = (UINT8)((Row * 255u) / (TitleH - 1u));
    return UiBlendRgb(Top, Bot, T);
}

UINT32 WindowBorderColor(int Idx) {
    if (!gWindows[Idx].Active) {
        return ThemeWindowBorderIdle();
    }
    if (Idx == gFocusWin) {
        return ThemeWindowBorderFocus();
    }
    if (Idx == gHoverWin) {
        return ThemeWindowBorderHover();
    }
    return ThemeWindowBorderIdle();
}

/* 最顶层（数组下标最大）命中窗；无则 -1 */
int TopWindowAt(UINT32 X, UINT32 Y) {
    int i;
    int Top = -1;

    for (i = 0; i < MAX_WINS; i++) {
        if (PointInWindow(&gWindows[i], X, Y)) {
            Top = i;
        }
    }
    return Top;
}

void GuiHoverUpdate(UINT32 X, UINT32 Y) {
    int Next = TopWindowAt(X, Y);
    int Prev = gHoverWin;

    if (Next == Prev) {
        return;
    }
    gHoverWin = Next;
    /* 焦点窗标题已是 Focus 色，悬停进出不必重画它的 chrome */
    if ((Prev >= 0 && Prev != gFocusWin && Prev < MAX_WINS && gWindows[Prev].Active) ||
        (Next >= 0 && Next != gFocusWin && Next < MAX_WINS && gWindows[Next].Active)) {
        GuiFrameBufferBegin();
        if (Prev >= 0 && Prev < MAX_WINS && Prev != gFocusWin && gWindows[Prev].Active) {
            DrawWindowChromeAt(Prev);
        }
        if (Next >= 0 && Next < MAX_WINS && Next != gFocusWin && gWindows[Next].Active) {
            DrawWindowChromeAt(Next);
        }
        GuiFrameBufferEnd();
    }
}


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

/* PR-GUI-l2-shadow：半透明遮挡填充 */
static void BlendFillRectOccluded(int Idx, UINT32 X, UINT32 Y, UINT32 W, UINT32 H,
                                  UINT32 Color, UINT8 Alpha) {
    UINT32 Row;
    UINT32 Col;
    UINT32 RunStart;
    int InRun;

    if (!W || !H || Alpha == 0) {
        return;
    }
    if (Alpha == 255) {
        FillRectOccluded(Idx, X, Y, W, H, Color);
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
                HalVideoBlendFillRect(X + RunStart, Py, Col - RunStart, 1,
                                      Color, Alpha);
                InRun = 0;
            }
        }
        if (InRun) {
            HalVideoBlendFillRect(X + RunStart, Py, W - RunStart, 1, Color, Alpha);
        }
    }
}

void ExpandRectByWindowShadow(UINT32 *X, UINT32 *Y, UINT32 *W, UINT32 *H) {
    UINT32 N = ThemeWindowShadowSize();

    (void)X;
    (void)Y;
    if (N == 0 || W == 0 || H == 0 || *W == 0 || *H == 0) {
        return;
    }
    /* drop shadow 只向右/下扩展 */
    *W += N;
    *H += N;
}

/*
 * 阴影落点之下：优先更低 z 窗备份，否则桌面（含图标）。
 * 禁止对 FB 二次 Blend（Sync/Raise 会叠成全黑）。
 */
static UINT32 ShadowUnderPixel(int ShadowIdx, UINT32 Px, UINT32 Py) {
    int i;
    UINT32 Icon;

    for (i = ShadowIdx - 1; i >= 0; i--) {
        if (!gWindows[i].Active) {
            continue;
        }
        if (Px >= gWindows[i].X && Py >= gWindows[i].Y &&
            Px < gWindows[i].X + gWindows[i].Width &&
            Py < gWindows[i].Y + gWindows[i].Height) {
            if (gWinBackupValid[i] && gWinBackup[i] != 0) {
                return SampleWindowBackupPixel(i, Px, Py);
            }
            return AnalyticWindowPixel(i, Px, Py);
        }
    }
    if (DesktopSamplePixel(Px, Py, &Icon)) {
        return Icon;
    }
    return DesktopBgAt(Px, Py);
}

static void BlendShadowSpan(int Idx, UINT32 X, UINT32 Y, UINT32 W, UINT8 Alpha,
                            UINT32 Color) {
    UINT32 Col;
    UINT32 Px;

    if (W == 0 || Y >= gScreenHeight || X >= gScreenWidth) {
        return;
    }
    if (X + W > gScreenWidth) {
        W = gScreenWidth - X;
    }
    for (Col = 0; Col < W; Col++) {
        Px = X + Col;
        if (PixelOccludedByAbove(Idx, Px, Y)) {
            continue;
        }
        HalVideoDrawPixel(Px, Y,
                          HalVideoBlendRgb(ShadowUnderPixel(Idx, Px, Y), Color, Alpha));
    }
}

void DrawWindowShadowAt(int Idx) {
    const GUI_WINDOW *W;
    UINT32 N;
    UINT8 MaxA;
    UINT32 Color;
    UINT32 d;

    if (Idx < 0 || Idx >= MAX_WINS) {
        return;
    }
    W = &gWindows[Idx];
    if (!W->Active || W->Width == 0 || W->Height == 0) {
        return;
    }
    N = ThemeWindowShadowSize();
    MaxA = ThemeWindowShadowMaxAlpha();
    Color = ThemeWindowShadowColor();
    if (N == 0 || MaxA == 0) {
        return;
    }
    HalVideoClearClip();
    for (d = 0; d < N; d++) {
        UINT8 A = (UINT8)(((UINT32)MaxA * (N - d)) / N);

        /*
         * 底/右/角：相对「干净 under」混合，Raise/Sync 重画可幂等。
         * 半压邻窗时 under 取邻窗备份，不吸入上层。
         */
        BlendShadowSpan(Idx, W->X + d, W->Y + W->Height + d, W->Width, A, Color);
        {
            UINT32 Row;
            for (Row = 0; Row < W->Height; Row++) {
                BlendShadowSpan(Idx, W->X + W->Width + d, W->Y + d + Row, 1, A, Color);
            }
        }
        BlendShadowSpan(Idx, W->X + W->Width + d, W->Y + W->Height + d, 1, A, Color);
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


/*
 * ClearDesktop：先铺桌面再贴窗。拖动结束后必须清底，否则旧 footprint 外的
 * 标题栏/关闭钮残影不会被「只贴窗矩形」的路径擦掉。
 * 阴影第二遍统一画：避免「先画 A 影到 B 上，再 WriteRect B」次序错乱，
 * 也避免 PaintWindowFromBackup 把影写进下层后再被 Backup 吸入。
 */
void SyncWindowVisualsEx(int ClearDesktop) {
    int i;

    ComposeBegin();
    GfxIrqEnter();
    CursorRestore();
    GfxIrqLeave();
    HalVideoClearClip();
    if (ClearDesktop) {
        DesktopFillRect(0, 0, gScreenWidth, gScreenHeight);
        DesktopDraw();
    }
    for (i = 0; i < MAX_WINS; i++) {
        if (!gWindows[i].Active) {
            continue;
        }
        if (gWinBackupValid[i] && gWinBackup[i] != 0) {
            PaintWindowFromBackup(i);
        } else if (!ClearDesktop && WindowOccludedByOther(i)) {
            /*
             * 未清桌面时：被挡窗勿 DrawWindowAt（会把露出客户区抹灰）。
             * 已清桌面时：必须满窗覆盖，否则桌面图标会透进客户区（空色块）。
             */
            DrawWindowChromeAt(i);
        } else {
            /* 本体；阴影留第二遍 */
            DrawWindowAtEx(i, WindowOccludedByOther(i) ? 1 : 0);
        }
    }
    for (i = 0; i < MAX_WINS; i++) {
        if (gWindows[i].Active) {
            DrawWindowShadowAt(i);
        }
    }
    GfxIrqEnter();
    CursorPaint();
    HalVideoPresent(); /* PR-G9：合成结束提交脏区 */
    GfxIrqLeave();
    ComposeEnd();
}


void SyncWindowVisuals(void) {
    SyncWindowVisualsEx(0);
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


void DrawWindowAtEx(int Idx, int Occlude) {
    const GUI_WINDOW *W = &gWindows[Idx];
    UINT32 Border;
    UINT32 Th;
    UINT32 Row;

    if (!W->Active) {
        return;
    }
    Border = WindowBorderColor(Idx);
    Th = TITLE_HEIGHT;
    if (Th > W->Height) {
        Th = W->Height;
    }
    /* 标题在客户区外；若仍开着 Shell/Settings clip，DrawString 会被裁掉 */
    HalVideoClearClip();
    /* 阴影由调用方或 Sync 第二遍画，避免与下层 WriteRect 次序打架 */
    if (Occlude) {
        for (Row = 0; Row < Th; Row++) {
            FillRectOccluded(Idx, W->X, W->Y + Row, W->Width, 1,
                             TitleBarColorAtRow(Idx, Row, Th));
        }
        DrawHLineOccluded(Idx, W->X, W->X + W->Width - 1, W->Y, Border);
        DrawHLineOccluded(Idx, W->X, W->X + W->Width - 1, W->Y + W->Height - 1,
                          Border);
        DrawVLineOccluded(Idx, W->X, W->Y, W->Y + W->Height - 1, Border);
        DrawVLineOccluded(Idx, W->X + W->Width - 1, W->Y, W->Y + W->Height - 1,
                          Border);
        if (W->Width > 2 && W->Height > TITLE_HEIGHT + 1) {
            FillRectOccluded(Idx, W->X + 1, W->Y + TITLE_HEIGHT, W->Width - 2,
                             W->Height - TITLE_HEIGHT - 1, W->Background);
        }
        DrawTitleStringOccluded(Idx, W);
        DrawCloseButton(Idx, W);
        return;
    }
    /*
     * 不透明整窗（主题自下而上合成用）：上层稍后覆盖，勿 Occlude，
     * 否则重叠区不画 → 标题镂空、客户区换色不全。
     */
    for (Row = 0; Row < Th; Row++) {
        HalVideoFillRect(W->X, W->Y + Row, W->Width, 1,
                         TitleBarColorAtRow(Idx, Row, Th));
    }
    HalVideoFillRect(W->X, W->Y, W->Width, 1, Border);
    HalVideoFillRect(W->X, W->Y + W->Height - 1, W->Width, 1, Border);
    HalVideoFillRect(W->X, W->Y, 1, W->Height, Border);
    HalVideoFillRect(W->X + W->Width - 1, W->Y, 1, W->Height, Border);
    if (W->Width > 2 && W->Height > TITLE_HEIGHT + 1) {
        HalVideoFillRect(W->X + 1, W->Y + TITLE_HEIGHT, W->Width - 2,
                         W->Height - TITLE_HEIGHT - 1, W->Background);
    }
    if (W->Title != 0 && W->Title[0] != 0) {
        HalVideoDrawStringAt(W->X + 8, W->Y + 4, W->Title, ThemeWindowTitleText());
    }
    /* 关闭钮也整块画，勿 Occlude（否则未聚焦 Shell 的 × 可能缺块） */
    {
        UINT32 Bx;
        UINT32 By;
        UINT32 Bw;
        UINT32 Bh;
        UINT32 Pad;
        UINT32 I;
        UINT32 Span;
        UINT32 Edge = ThemeWindowBorderFocus();

        CloseButtonRect(W, &Bx, &By, &Bw, &Bh);
        HalVideoFillRect(Bx, By, Bw, Bh, ThemeCloseButton());
        if (Bw >= 2 && Bh >= 2) {
            HalVideoFillRect(Bx, By, Bw, 1, Edge);
            HalVideoFillRect(Bx, By + Bh - 1, Bw, 1, Edge);
            HalVideoFillRect(Bx, By, 1, Bh, Edge);
            HalVideoFillRect(Bx + Bw - 1, By, 1, Bh, Edge);
        }
        Pad = 7;
        if (Bw > Pad * 2 + 2 && Bh > Pad * 2 + 2) {
            Span = Bw - 1 - Pad * 2;
            for (I = 0; I <= Span; I++) {
                HalVideoDrawPixelRaw(Bx + Pad + I, By + Pad + I, ThemeWindowTitleText());
                HalVideoDrawPixelRaw(Bx + Bw - 1 - Pad - I, By + Pad + I,
                                     ThemeWindowTitleText());
            }
        }
    }
}


void DrawWindowAt(int Idx) {
    DrawWindowAtEx(Idx, 1);
    DrawWindowShadowAt(Idx);
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


void FillDesktopRectClipped(UINT32 X, UINT32 Y, UINT32 W, UINT32 H) {
    UINT32 Row;
    UINT32 Col;
    UINT32 RunStart;
    int InRun;

    if (!W || !H) {
        return;
    }
    for (Row = Y; Row < Y + H; Row++) {
        InRun = 0;
        RunStart = 0;
        for (Col = X; Col < X + W; Col++) {
            int Cover = PointInAnyActiveWindow(Col, Row);
            if (!Cover && !InRun) {
                RunStart = Col;
                InRun = 1;
            } else if (Cover && InRun) {
                if (Col > RunStart) {
                    DesktopFillRect(RunStart, Row, Col - RunStart, 1);
                }
                InRun = 0;
            }
        }
        if (InRun && X + W > RunStart) {
            DesktopFillRect(RunStart, Row, X + W - RunStart, 1);
        }
    }
}


int WindowOccludedByOther(int Idx) {
    int j;

    for (j = Idx + 1; j < MAX_WINS; j++) {
        if (!gWindows[j].Active) {
            continue;
        }
        if (RectIntersects(gWindows[Idx].X, gWindows[Idx].Y, gWindows[Idx].Width, gWindows[Idx].Height,
                           gWindows[j].X, gWindows[j].Y, gWindows[j].Width, gWindows[j].Height)) {
            return 1;
        }
    }
    return 0;
}

