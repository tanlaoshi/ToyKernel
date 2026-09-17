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

/* PR-GUI-l2-round：局部像素是否在圆角矩形内（与 UI 圆角判定一致） */
int PixelInWindowRound(UINT32 Lx, UINT32 Ly, UINT32 Ww, UINT32 Wh, UINT32 R) {
    INT32 Dx;
    INT32 Dy;
    INT32 R2;

    if (Ww == 0 || Wh == 0 || Lx >= Ww || Ly >= Wh) {
        return 0;
    }
    if (R == 0) {
        return 1;
    }
    if (R > Ww / 2) {
        R = Ww / 2;
    }
    if (R > Wh / 2) {
        R = Wh / 2;
    }
    R2 = (INT32)(R * R);
    if (Lx < R && Ly < R) {
        Dx = (INT32)Lx - (INT32)R;
        Dy = (INT32)Ly - (INT32)R;
        return (Dx * Dx + Dy * Dy) <= R2;
    }
    if (Lx > Ww - 1 - R && Ly < R) {
        Dx = (INT32)Lx - (INT32)(Ww - 1 - R);
        Dy = (INT32)Ly - (INT32)R;
        return (Dx * Dx + Dy * Dy) <= R2;
    }
    if (Lx < R && Ly > Wh - 1 - R) {
        Dx = (INT32)Lx - (INT32)R;
        Dy = (INT32)Ly - (INT32)(Wh - 1 - R);
        return (Dx * Dx + Dy * Dy) <= R2;
    }
    if (Lx > Ww - 1 - R && Ly > Wh - 1 - R) {
        Dx = (INT32)Lx - (INT32)(Ww - 1 - R);
        Dy = (INT32)Ly - (INT32)(Wh - 1 - R);
        return (Dx * Dx + Dy * Dy) <= R2;
    }
    return 1;
}

static UINT32 ClampWindowRadius(UINT32 Ww, UINT32 Wh, UINT32 R) {
    if (R == 0 || Ww < 4 || Wh < 4) {
        return 0;
    }
    if (R > Ww / 2) {
        R = Ww / 2;
    }
    if (R > Wh / 2) {
        R = Wh / 2;
    }
    return R;
}

/* 只扫四角 R×R；中间矩形走 FillRect（规格：中间仍矩形） */
static void FillCornerDisk(int Idx, int Occlude, UINT32 Ox, UINT32 Oy, UINT32 R,
                           INT32 Cx, INT32 Cy, UINT32 Color) {
    UINT32 Row;
    UINT32 Col;
    INT32 R2 = (INT32)(R * R);

    for (Row = 0; Row < R; Row++) {
        for (Col = 0; Col < R; Col++) {
            INT32 Dx = (INT32)Col - Cx;
            INT32 Dy = (INT32)Row - Cy;
            UINT32 Px;
            UINT32 Py;

            if (Dx * Dx + Dy * Dy > R2) {
                continue;
            }
            Px = Ox + Col;
            Py = Oy + Row;
            if (Occlude) {
                if (!PixelOccludedByAbove(Idx, Px, Py)) {
                    HalVideoDrawPixelRaw(Px, Py, Color);
                }
            } else {
                HalVideoDrawPixelRaw(Px, Py, Color);
            }
        }
    }
}

static void FillRoundRectBody(int Idx, int Occlude, UINT32 X, UINT32 Y,
                              UINT32 W, UINT32 H, UINT32 R, UINT32 Color) {
    if (R == 0) {
        if (Occlude) {
            FillRectOccluded(Idx, X, Y, W, H, Color);
        } else {
            HalVideoFillRect(X, Y, W, H, Color);
        }
        return;
    }
    /* 中间竖条 + 左右腰 */
    if (W > 2 * R) {
        if (Occlude) {
            FillRectOccluded(Idx, X + R, Y, W - 2 * R, H, Color);
        } else {
            HalVideoFillRect(X + R, Y, W - 2 * R, H, Color);
        }
    }
    if (H > 2 * R) {
        if (Occlude) {
            FillRectOccluded(Idx, X, Y + R, R, H - 2 * R, Color);
            FillRectOccluded(Idx, X + W - R, Y + R, R, H - 2 * R, Color);
        } else {
            HalVideoFillRect(X, Y + R, R, H - 2 * R, Color);
            HalVideoFillRect(X + W - R, Y + R, R, H - 2 * R, Color);
        }
    }
    /* 四角：圆心相对角块为 (R,R)/(R-1,R)/(R,R-1)/(R-1,R-1) */
    FillCornerDisk(Idx, Occlude, X, Y, R, (INT32)R, (INT32)R, Color);                 /* TL */
    FillCornerDisk(Idx, Occlude, X + W - R, Y, R, (INT32)R - 1, (INT32)R, Color);     /* TR */
    FillCornerDisk(Idx, Occlude, X, Y + H - R, R, (INT32)R, (INT32)R - 1, Color);     /* BL */
    FillCornerDisk(Idx, Occlude, X + W - R, Y + H - R, R, (INT32)R - 1, (INT32)R - 1,
                   Color);                                                           /* BR */
}

static void DrawRoundBorder(int Idx, int Occlude, UINT32 X, UINT32 Y,
                            UINT32 W, UINT32 H, UINT32 R, UINT32 Color) {
    INT32 r;
    INT32 cx1;
    INT32 cy1;
    INT32 cx2;
    INT32 cy2;
    INT32 cx3;
    INT32 cy3;
    INT32 cx4;
    INT32 cy4;
    INT32 x;
    INT32 y;
    INT32 d;

    if (W < 2 || H < 2) {
        return;
    }
    if (R == 0) {
        if (Occlude) {
            DrawHLineOccluded(Idx, X, X + W - 1, Y, Color);
            DrawHLineOccluded(Idx, X, X + W - 1, Y + H - 1, Color);
            DrawVLineOccluded(Idx, X, Y, Y + H - 1, Color);
            DrawVLineOccluded(Idx, X + W - 1, Y, Y + H - 1, Color);
        } else {
            HalVideoFillRect(X, Y, W, 1, Color);
            HalVideoFillRect(X, Y + H - 1, W, 1, Color);
            HalVideoFillRect(X, Y, 1, H, Color);
            HalVideoFillRect(X + W - 1, Y, 1, H, Color);
        }
        return;
    }
    if (Occlude) {
        DrawHLineOccluded(Idx, X + R, X + W - 1 - R, Y, Color);
        DrawHLineOccluded(Idx, X + R, X + W - 1 - R, Y + H - 1, Color);
        DrawVLineOccluded(Idx, X, Y + R, Y + H - 1 - R, Color);
        DrawVLineOccluded(Idx, X + W - 1, Y + R, Y + H - 1 - R, Color);
    } else {
        HalVideoFillRect(X + R, Y, W - 2 * R, 1, Color);
        HalVideoFillRect(X + R, Y + H - 1, W - 2 * R, 1, Color);
        HalVideoFillRect(X, Y + R, 1, H - 2 * R, Color);
        HalVideoFillRect(X + W - 1, Y + R, 1, H - 2 * R, Color);
    }
    r = (INT32)R;
    cx1 = (INT32)(X + R);
    cy1 = (INT32)(Y + R);
    cx2 = (INT32)(X + W - 1 - R);
    cy2 = (INT32)(Y + R);
    cx3 = (INT32)(X + R);
    cy3 = (INT32)(Y + H - 1 - R);
    cx4 = (INT32)(X + W - 1 - R);
    cy4 = (INT32)(Y + H - 1 - R);
    x = 0;
    y = r;
    d = 3 - 2 * r;
    while (x <= y) {
        UINT32 Pts[8][2];
        UINT32 Pi;

        Pts[0][0] = (UINT32)(cx1 - x);
        Pts[0][1] = (UINT32)(cy1 - y);
        Pts[1][0] = (UINT32)(cx1 - y);
        Pts[1][1] = (UINT32)(cy1 - x);
        Pts[2][0] = (UINT32)(cx2 + x);
        Pts[2][1] = (UINT32)(cy2 - y);
        Pts[3][0] = (UINT32)(cx2 + y);
        Pts[3][1] = (UINT32)(cy2 - x);
        Pts[4][0] = (UINT32)(cx3 - x);
        Pts[4][1] = (UINT32)(cy3 + y);
        Pts[5][0] = (UINT32)(cx3 - y);
        Pts[5][1] = (UINT32)(cy3 + x);
        Pts[6][0] = (UINT32)(cx4 + x);
        Pts[6][1] = (UINT32)(cy4 + y);
        Pts[7][0] = (UINT32)(cx4 + y);
        Pts[7][1] = (UINT32)(cy4 + x);
        for (Pi = 0; Pi < 8; Pi++) {
            if (Occlude && PixelOccludedByAbove(Idx, Pts[Pi][0], Pts[Pi][1])) {
                continue;
            }
            HalVideoDrawPixelRaw(Pts[Pi][0], Pts[Pi][1], Color);
        }
        if (d < 0) {
            d += 4 * x + 6;
        } else {
            d += 4 * (x - y) + 10;
            y--;
        }
        x++;
    }
}

static void PaintWindowChromeRound(int Idx, int Occlude, int PaintClient) {
    const GUI_WINDOW *W = &gWindows[Idx];
    UINT32 Border = WindowBorderColor(Idx);
    UINT32 Title = TitleBarColor(Idx);
    UINT32 R = ClampWindowRadius(W->Width, W->Height, ThemeWindowCornerRadius());
    UINT32 Th = TITLE_HEIGHT;

    if (Th > W->Height) {
        Th = W->Height;
    }
    /* 先整窗圆角客户底，再盖标题（顶角同半径，避免直角露底） */
    if (PaintClient) {
        FillRoundRectBody(Idx, Occlude, W->X, W->Y, W->Width, W->Height, R,
                          W->Background);
    }
    if (Th > 0) {
        if (R == 0) {
            if (Occlude) {
                FillRectOccluded(Idx, W->X, W->Y, W->Width, Th, Title);
            } else {
                HalVideoFillRect(W->X, W->Y, W->Width, Th, Title);
            }
        } else {
            /* 标题带：中间 + 顶两角；腰部左右到 Th */
            if (W->Width > 2 * R) {
                if (Occlude) {
                    FillRectOccluded(Idx, W->X + R, W->Y, W->Width - 2 * R, Th, Title);
                } else {
                    HalVideoFillRect(W->X + R, W->Y, W->Width - 2 * R, Th, Title);
                }
            }
            if (Th > R) {
                if (Occlude) {
                    FillRectOccluded(Idx, W->X, W->Y + R, R, Th - R, Title);
                    FillRectOccluded(Idx, W->X + W->Width - R, W->Y + R, R, Th - R,
                                     Title);
                } else {
                    HalVideoFillRect(W->X, W->Y + R, R, Th - R, Title);
                    HalVideoFillRect(W->X + W->Width - R, W->Y + R, R, Th - R, Title);
                }
            }
            FillCornerDisk(Idx, Occlude, W->X, W->Y, R, (INT32)R, (INT32)R, Title);
            FillCornerDisk(Idx, Occlude, W->X + W->Width - R, W->Y, R, (INT32)R - 1,
                           (INT32)R, Title);
        }
    }
    DrawRoundBorder(Idx, Occlude, W->X, W->Y, W->Width, W->Height, R, Border);
}

/*
 * 客户区 / 备份 WriteRect 是直角 AABB，会盖住圆角外切角。
 * 把四角圆外像素恢复为桌面（含壁纸），上层窗遮挡则跳过。
 */
void PunchWindowRoundExterior(int Idx) {
    const GUI_WINDOW *W;
    UINT32 R;
    UINT32 Col;
    UINT32 Row;
    UINT32 Ww;
    UINT32 Wh;

    if (Idx < 0 || Idx >= MAX_WINS) {
        return;
    }
    W = &gWindows[Idx];
    if (!W->Active || W->Width == 0 || W->Height == 0) {
        return;
    }
    Ww = W->Width;
    Wh = W->Height;
    R = ClampWindowRadius(Ww, Wh, ThemeWindowCornerRadius());
    if (R == 0) {
        return;
    }
    HalVideoClearClip();
    for (Row = 0; Row < R; Row++) {
        for (Col = 0; Col < R; Col++) {
            UINT32 Corners[4][2];
            UINT32 Ci;

            Corners[0][0] = Col;
            Corners[0][1] = Row;
            Corners[1][0] = Ww - R + Col;
            Corners[1][1] = Row;
            Corners[2][0] = Col;
            Corners[2][1] = Wh - R + Row;
            Corners[3][0] = Ww - R + Col;
            Corners[3][1] = Wh - R + Row;
            for (Ci = 0; Ci < 4; Ci++) {
                UINT32 Lx = Corners[Ci][0];
                UINT32 Ly = Corners[Ci][1];
                UINT32 Px;
                UINT32 Py;

                if (PixelInWindowRound(Lx, Ly, Ww, Wh, R)) {
                    continue;
                }
                Px = W->X + Lx;
                Py = W->Y + Ly;
                if (Px >= gScreenWidth || Py >= gScreenHeight) {
                    continue;
                }
                if (PixelOccludedByAbove(Idx, Px, Py)) {
                    continue;
                }
                HalVideoDrawPixelRaw(Px, Py, DesktopBgAt(Px, Py));
            }
        }
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

        /* 底边：右移 d；右边：下移 d；角点单独补 */
        BlendFillRectOccluded(Idx, W->X + d, W->Y + W->Height + d,
                              W->Width, 1, Color, A);
        BlendFillRectOccluded(Idx, W->X + W->Width + d, W->Y + d,
                              1, W->Height, Color, A);
        BlendFillRectOccluded(Idx, W->X + W->Width + d, W->Y + W->Height + d,
                              1, 1, Color, A);
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
            DrawWindowAt(i);
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

    if (!W->Active) {
        return;
    }
    /* 标题在客户区外；若仍开着 Shell/Settings clip，DrawString 会被裁掉 */
    HalVideoClearClip();
    /* 先画阴影（窗外），再画圆角本体；上层窗稍后覆盖 */
    DrawWindowShadowAt(Idx);
    PaintWindowChromeRound(Idx, Occlude, 1 /* PaintClient */);
    if (Occlude) {
        DrawTitleStringOccluded(Idx, W);
        DrawCloseButton(Idx, W);
        PunchWindowRoundExterior(Idx);
        return;
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
    PunchWindowRoundExterior(Idx);
}


void DrawWindowAt(int Idx) {
    DrawWindowAtEx(Idx, 1);
}


/* 仅重绘标题栏与边框，保留客户区已有文字；不画到上层窗口上 */
void DrawWindowChromeAt(int Idx) {
    const GUI_WINDOW *W = &gWindows[Idx];

    if (!W->Active) {
        return;
    }
    HalVideoClearClip();
    /* 不重填客户区，只圆角标题+边框（避免直角露底） */
    PaintWindowChromeRound(Idx, 1, 0 /* !PaintClient */);
    DrawTitleStringOccluded(Idx, W);
    DrawCloseButton(Idx, W);
    PunchWindowRoundExterior(Idx);
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

