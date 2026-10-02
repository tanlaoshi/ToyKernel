/*
 * GuiBackupSample.c — 备份像素采样与上层覆盖判断
 * 核心：GuiBackup.c
 */
#include "GuiPrivate.h"
#include "UI.h"
#include "HalVideo.h"
#include "Hal.h"
#include "PhysicalMemory.h"
#include "Theme.h"
#include "Font.h"
#include "Desktop.h"

/* 与 DrawWindowAt 布局一致；仅作无备份时的回退 */
UINT32 AnalyticWindowPixel(int Idx, UINT32 Px, UINT32 Py) {
    const GUI_WINDOW *W = &gWindows[Idx];
    UINT32 Lx;
    UINT32 Ly;

    if (!W->Active) {
        return DesktopBgAt(Px, Py);
    }
    if (Px < W->X || Py < W->Y || Px >= W->X + W->Width || Py >= W->Y + W->Height) {
        return DesktopBgAt(Px, Py);
    }
    Lx = Px - W->X;
    Ly = Py - W->Y;
    if (Ly < TITLE_HEIGHT) {
        UINT32 Th = TITLE_HEIGHT;

        if (Th > W->Height) {
            Th = W->Height;
        }
        return TitleBarColorAtRow(Idx, Ly, Th);
    }
    if (Ly == W->Height - 1 || Lx == 0 || Lx == W->Width - 1) {
        return WindowBorderColor(Idx);
    }
    /* 与 DrawWindowAt 一致：白边内侧整片客户区底色 */
    return W->Background;
}

UINT32 SampleWindowBackupPixel(int Idx, UINT32 Px, UINT32 Py) {
    const GUI_WINDOW *W = &gWindows[Idx];
    UINT32 Bw;
    UINT32 Bh;
    UINT32 Lx;
    UINT32 Ly;

    if (!gWinBackupValid[Idx] || gWinBackup[Idx] == 0 || !W->Active) {
        return DesktopBgAt(Px, Py);
    }
    Bw = gWinBackupW[Idx];
    Bh = gWinBackupH[Idx];
    if (Px < W->X || Py < W->Y) {
        return DesktopBgAt(Px, Py);
    }
    Lx = Px - W->X;
    Ly = Py - W->Y;
    if (Lx >= Bw || Ly >= Bh) {
        return DesktopBgAt(Px, Py);
    }
    return gWinBackup[Idx][Ly * Bw + Lx];
}


int WindowBackupCoversPixel(int Idx, UINT32 Px, UINT32 Py) {
    const GUI_WINDOW *W = &gWindows[Idx];

    if (!gWinBackupValid[Idx] || !W->Active) {
        return 0;
    }
    return Px >= W->X && Py >= W->Y &&
           Px < W->X + gWinBackupW[Idx] && Py < W->Y + gWinBackupH[Idx];
}


/*
 * 更高 z 是否盖住该像素。含 drop-shadow 右/下扩边：影落在下层窗体内时
 * 若只按窗 AABB 判断，Backup ReadPixel 会吸入阴影 → 松手烙印。
 */
int PixelCoveredByHigherWindow(int Idx, UINT32 Px, UINT32 Py) {
    int j;
    UINT32 N = ThemeWindowShadowSize();

    for (j = Idx + 1; j < MAX_WINS; j++) {
        const GUI_WINDOW *W = &gWindows[j];
        UINT32 X1;
        UINT32 Y1;

        if (!W->Active) {
            continue;
        }
        if (PointInWindow(W, Px, Py)) {
            return 1;
        }
        if (N == 0) {
            continue;
        }
        X1 = W->X + W->Width + N;
        Y1 = W->Y + W->Height + N;
        if (Px >= W->X && Px < X1 && Py >= W->Y && Py < Y1) {
            return 1;
        }
    }
    return 0;
}

/* 体相交或上层阴影扩边扫过本窗 → 禁止 ForceFull ReadRect */
int WindowOccludedByOtherOrShadow(int Idx) {
    int j;
    UINT32 N = ThemeWindowShadowSize();

    if (WindowOccludedByOther(Idx)) {
        return 1;
    }
    if (N == 0 || Idx < 0 || Idx >= MAX_WINS || !gWindows[Idx].Active) {
        return 0;
    }
    for (j = Idx + 1; j < MAX_WINS; j++) {
        if (!gWindows[j].Active) {
            continue;
        }
        if (RectIntersects(gWindows[Idx].X, gWindows[Idx].Y,
                           gWindows[Idx].Width, gWindows[Idx].Height,
                           gWindows[j].X, gWindows[j].Y,
                           gWindows[j].Width + N, gWindows[j].Height + N)) {
            return 1;
        }
    }
    return 0;
}


void GuiBackupSyncRect(UINT32 X, UINT32 Y, UINT32 W, UINT32 H) {
    const GUI_WINDOW *Win;
    UINT32 Row;
    UINT32 Col;
    UINT32 Bw;
    UINT32 Bh;
    UINT32 Px;
    UINT32 Py;
    UINT32 Bx;
    UINT32 By;
    UINT32 Dst;
    int Idx;

    if (gFocusWin < 0 || gFocusWin >= MAX_WINS || !gWindows[gFocusWin].Active) {
        return;
    }
    Idx = gFocusWin;
    if (!gWinBackupValid[Idx] || gWinBackup[Idx] == 0) {
        return;
    }
    Win = &gWindows[Idx];
    Bw = gWinBackupW[Idx];
    Bh = gWinBackupH[Idx];
    if (Bw == 0 || Bh == 0) {
        return;
    }
    for (Row = 0; Row < H; Row++) {
        Py = Y + Row;
        if (Py < Win->Y || Py >= Win->Y + Bh) {
            continue;
        }
        for (Col = 0; Col < W; Col++) {
            Px = X + Col;
            if (Px < Win->X || Px >= Win->X + Bw) {
                continue;
            }
            if (PixelCoveredByHigherWindow(Idx, Px, Py)) {
                continue;
            }
            Bx = Px - Win->X;
            By = Py - Win->Y;
            Dst = By * Bw + Bx;
            gWinBackup[Idx][Dst] = HalVideoReadPixel(Px, Py);
        }
    }
}
