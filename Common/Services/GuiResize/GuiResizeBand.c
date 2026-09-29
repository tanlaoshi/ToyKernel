/*
 * GuiResizeBand.c — 线框预览与尺寸计算（PR-S3-guiresize-1）
 *
 * 从 GuiResize.c 原样搬家；不改语义。
 */
#include "GuiPrivate.h"
#include "UI.h"
#include "Theme.h"
#include "Desktop.h"
#include "SettingsUi.h"
#include "FilesUi.h"
#include "StoreUi.h"
#include "DevicesUi.h"
#include "EditUi.h"
#include "TtyUi.h"

#define RESIZE_MIN_W 480u
#define RESIZE_MIN_H 360u

static void ClampResizeSize(const GUI_WINDOW *W, UINT32 *OutW, UINT32 *OutH) {
    UINT32 MaxW;
    UINT32 MaxH;
    UINT32 Nw;
    UINT32 Nh;
    UINT32 MinW = RESIZE_MIN_W;
    UINT32 MinH = RESIZE_MIN_H;

    Nw = *OutW;
    Nh = *OutH;
    /*
     * 已小于课设最小的窗（如 TaskMgr 高 200）：勿一碰拖拽就撑到 360（≈两倍）。
     * 地板取「当前尺寸」与绝对下限 160×100 之间。
     */
    if (W->Width < MinW) {
        MinW = W->Width;
    }
    if (W->Height < MinH) {
        MinH = W->Height;
    }
    if (MinW < 160u) {
        MinW = 160u;
    }
    if (MinH < 100u) {
        MinH = 100u;
    }
    if (Nw < MinW) {
        Nw = MinW;
    }
    if (Nh < MinH) {
        Nh = MinH;
    }
    MaxW = (W->X < gScreenWidth) ? (gScreenWidth - W->X) : MinW;
    MaxH = (W->Y < gScreenHeight) ? (gScreenHeight - W->Y) : MinH;
    if (Nw > MaxW) {
        Nw = MaxW;
    }
    if (Nh > MaxH) {
        Nh = MaxH;
    }
    if (Nw < MinW) {
        Nw = (MaxW < MinW) ? MaxW : MinW;
    }
    if (Nh < MinH) {
        Nh = (MaxH < MinH) ? MaxH : MinH;
    }
    *OutW = Nw;
    *OutH = Nh;
}

void EraseResizeBand(int Idx, UINT32 Bw, UINT32 Bh) {
    const GUI_WINDOW *W = &gWindows[Idx];

    if (!gResizeBandOn || Bw == 0 || Bh == 0) {
        return;
    }
    GuiClearIconDragFootprint(W->X, W->Y, Bw, Bh);
    if (gWinBackupValid[Idx] && gWinBackup[Idx] != 0) {
        PaintWindowFromBackup(Idx);
        DrawWindowShadowAt(Idx);
    } else {
        DrawWindowAt(Idx);
    }
    gResizeBandOn = 0;
}

void DrawResizeBand(int Idx, UINT32 Bw, UINT32 Bh) {
    const GUI_WINDOW *W = &gWindows[Idx];

    if (Bw < 4 || Bh < 4) {
        return;
    }
    UiDrawRectangle(W->X, W->Y, Bw, Bh, COLOR_WHITE);
    if (Bw > 6 && Bh > 6) {
        UiDrawRectangle(W->X + 1, W->Y + 1, Bw - 2, Bh - 2, COLOR_YELLOW);
    }
    gResizeBandW = Bw;
    gResizeBandH = Bh;
    gResizeBandOn = 1;
}

void ComputeResizeSize(int Idx, int Edge, UINT32 X, UINT32 Y,
                       UINT32 *OutW, UINT32 *OutH) {
    const GUI_WINDOW *W;
    INT32 Dw;
    INT32 Dh;
    INT32 Tw;
    INT32 Th;
    UINT32 Nw;
    UINT32 Nh;

    if (Idx < 0 || Idx >= MAX_WINS) {
        *OutW = gResizeOrigW;
        *OutH = gResizeOrigH;
        return;
    }
    W = &gWindows[Idx];
    Dw = (INT32)X - gResizeAnchorX;
    Dh = (INT32)Y - gResizeAnchorY;
    Tw = (INT32)gResizeOrigW;
    Th = (INT32)gResizeOrigH;
    if (Edge == RESIZE_EDGE_SE || Edge == RESIZE_EDGE_E) {
        Tw = (INT32)gResizeOrigW + Dw;
    }
    if (Edge == RESIZE_EDGE_SE || Edge == RESIZE_EDGE_S) {
        Th = (INT32)gResizeOrigH + Dh;
    }
    /* 向左/上拖过最小时 INT 变负；若直接转 UINT32 会下溢成「撑满屏」 */
    if (Tw < (INT32)RESIZE_MIN_W) {
        Tw = (INT32)RESIZE_MIN_W;
    }
    if (Th < (INT32)RESIZE_MIN_H) {
        Th = (INT32)RESIZE_MIN_H;
    }
    Nw = (UINT32)Tw;
    Nh = (UINT32)Th;
    ClampResizeSize(W, &Nw, &Nh);
    *OutW = Nw;
    *OutH = Nh;
}

void RepaintAfterResize(int Idx) {
    GUI_WIN_KIND Kind = gWindows[Idx].Kind;

    if (Kind == GUI_WIN_SETTINGS) {
        SettingsUiRepaint();
    } else if (Kind == GUI_WIN_STORE) {
        StoreUiRepaint();
    } else if (Kind == GUI_WIN_DEVICES) {
        DevicesUiRepaint();
    } else if (Kind == GUI_WIN_FILES) {
        FilesUiRepaint();
    } else if (Kind == GUI_WIN_EDIT) {
        EditUiRepaint();
    } else if (Kind == GUI_WIN_TTY) {
        TtyUiRepaint();
    } else if (Kind == GUI_WIN_USER) {
        PaintUserClient(Idx);
    } else if (Kind == GUI_WIN_SHELL) {
        GuiConsoleOpsPaintShellWindow(Idx);
    }
}
