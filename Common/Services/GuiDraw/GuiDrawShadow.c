/*
 * GuiDrawShadow.c — 窗口右下阴影
 * 核心：GuiDraw.c
 */
#include "GuiPrivate.h"
#include "UI.h"
#include "HalVideo.h"
#include "Hal.h"
#include "Theme.h"
#include "Font.h"
#include "Desktop.h"

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
