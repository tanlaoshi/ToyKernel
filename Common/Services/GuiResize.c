/*
 * GuiResize.c — PR-GUI-win-resize：右下角拖拽改大小（线框松手提交）
 */
#include "GuiPrivate.h"
#include "UI.h"
#include "HalVideo.h"
#include "Theme.h"
#include "Desktop.h"
#include "SettingsUi.h"
#include "FilesUi.h"
#include "StoreUi.h"
#include "DevicesUi.h"
#include "EditUi.h"

#define RESIZE_HOT    16u
#define RESIZE_MIN_W  480u
#define RESIZE_MIN_H  360u
#define RESIZE_MIN_STEP 3u

int    gResizeWin = -1;
int    gResizeArmed;
UINT32 gResizeOrigW;
UINT32 gResizeOrigH;
INT32  gResizeAnchorX;
INT32  gResizeAnchorY;
int    gResizeBandOn;
UINT32 gResizeBandW;
UINT32 gResizeBandH;

int PointInResizeCorner(const GUI_WINDOW *W, UINT32 X, UINT32 Y) {
    UINT32 Rx;
    UINT32 Ry;

    if (!W || !W->Active || W->Width < RESIZE_HOT || W->Height < RESIZE_HOT) {
        return 0;
    }
    Rx = W->X + W->Width - RESIZE_HOT;
    Ry = W->Y + W->Height - RESIZE_HOT;
    return (X >= Rx && X < W->X + W->Width && Y >= Ry && Y < W->Y + W->Height) ? 1
                                                                              : 0;
}

static void ClampResizeSize(const GUI_WINDOW *W, UINT32 *OutW, UINT32 *OutH) {
    UINT32 MaxW;
    UINT32 MaxH;
    UINT32 Nw;
    UINT32 Nh;

    Nw = *OutW;
    Nh = *OutH;
    if (Nw < RESIZE_MIN_W) {
        Nw = RESIZE_MIN_W;
    }
    if (Nh < RESIZE_MIN_H) {
        Nh = RESIZE_MIN_H;
    }
    MaxW = (W->X < gScreenWidth) ? (gScreenWidth - W->X) : RESIZE_MIN_W;
    MaxH = (W->Y < gScreenHeight) ? (gScreenHeight - W->Y) : RESIZE_MIN_H;
    if (Nw > MaxW) {
        Nw = MaxW;
    }
    if (Nh > MaxH) {
        Nh = MaxH;
    }
    if (Nw < RESIZE_MIN_W) {
        Nw = (MaxW < RESIZE_MIN_W) ? MaxW : RESIZE_MIN_W;
    }
    if (Nh < RESIZE_MIN_H) {
        Nh = (MaxH < RESIZE_MIN_H) ? MaxH : RESIZE_MIN_H;
    }
    *OutW = Nw;
    *OutH = Nh;
}

static void EraseResizeBand(int Idx, UINT32 Bw, UINT32 Bh) {
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

static void DrawResizeBand(int Idx, UINT32 Bw, UINT32 Bh) {
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

static void ComputeResizeSize(UINT32 X, UINT32 Y, UINT32 *OutW, UINT32 *OutH) {
    const GUI_WINDOW *W = &gWindows[gResizeWin];
    INT32 Dw;
    INT32 Dh;
    UINT32 Nw;
    UINT32 Nh;

    Dw = (INT32)X - gResizeAnchorX;
    Dh = (INT32)Y - gResizeAnchorY;
    Nw = (UINT32)((INT32)gResizeOrigW + Dw);
    Nh = (UINT32)((INT32)gResizeOrigH + Dh);
    ClampResizeSize(W, &Nw, &Nh);
    *OutW = Nw;
    *OutH = Nh;
}

static void RepaintAfterResize(int Idx) {
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
    } else if (Kind == GUI_WIN_USER) {
        PaintUserClient(Idx);
    } else if (Kind == GUI_WIN_SHELL) {
        GuiConsoleOpsPaintShellWindow(Idx);
    }
}

void GuiResizeBegin(int Idx, UINT32 X, UINT32 Y) {
    if (Idx < 0 || Idx >= MAX_WINS || !gWindows[Idx].Active) {
        return;
    }
    gDragWin = -1;
    gDragArmed = 0;
    gResizeWin = Idx;
    gResizeOrigW = gWindows[Idx].Width;
    gResizeOrigH = gWindows[Idx].Height;
    gResizeAnchorX = (INT32)X;
    gResizeAnchorY = (INT32)Y;
    gResizeArmed = 1;
    gResizeBandOn = 0;
    gResizeBandW = 0;
    gResizeBandH = 0;
}

void GuiResizeUpdate(UINT32 X, UINT32 Y) {
    UINT32 Nw;
    UINT32 Nh;
    UINT32 OdW;
    UINT32 OdH;

    if (gResizeWin < 0 || gResizeWin >= MAX_WINS || !gWindows[gResizeWin].Active) {
        return;
    }
    ComputeResizeSize(X, Y, &Nw, &Nh);
    OdW = (Nw > gResizeOrigW) ? (Nw - gResizeOrigW) : (gResizeOrigW - Nw);
    OdH = (Nh > gResizeOrigH) ? (Nh - gResizeOrigH) : (gResizeOrigH - Nh);
    if (gResizeArmed) {
        if (OdW < RESIZE_MIN_STEP && OdH < RESIZE_MIN_STEP) {
            return;
        }
        gResizeArmed = 0;
    }
    if (gResizeBandOn && Nw == gResizeBandW && Nh == gResizeBandH) {
        return;
    }

    ComposeBegin();
    GfxIrqEnter();
    CursorRestore();
    if (gResizeBandOn) {
        EraseResizeBand(gResizeWin, gResizeBandW, gResizeBandH);
    }
    DrawResizeBand(gResizeWin, Nw, Nh);
    CursorPaint();
    HalVideoPresent();
    GfxIrqLeave();
    ComposeEnd();
}

void GuiResizeEnd(void) {
    int Idx = gResizeWin;
    UINT32 Nw;
    UINT32 Nh;
    int Did;

    gResizeWin = -1;
    gResizeArmed = 0;
    if (Idx < 0 || Idx >= MAX_WINS || !gWindows[Idx].Active) {
        gResizeBandOn = 0;
        return;
    }

    Did = gResizeBandOn ||
          (gResizeBandW != 0 && gResizeBandH != 0);
    ComputeResizeSize(gCursorX, gCursorY, &Nw, &Nh);

    ComposeBegin();
    GfxIrqEnter();
    CursorRestore();
    if (gResizeBandOn) {
        EraseResizeBand(Idx, gResizeBandW, gResizeBandH);
    }

    if (Did && (Nw != gWindows[Idx].Width || Nh != gWindows[Idx].Height)) {
        UINT32 Ox = gWindows[Idx].X;
        UINT32 Oy = gWindows[Idx].Y;
        UINT32 Ow = gWindows[Idx].Width;
        UINT32 Oh = gWindows[Idx].Height;
        UINT32 Fx = Ox;
        UINT32 Fy = Oy;
        UINT32 Fw = Ow;
        UINT32 Fh = Oh;

        /* 先擦旧 footprint（含影），再提交新尺寸 */
        ExpandRectByWindowShadow(&Fx, &Fy, &Fw, &Fh);
        ClipRectToScreen(&Fx, &Fy, &Fw, &Fh);
        ClearOldDragFootprint(Fx, Fy, Fw, Fh, Idx);

        gWindows[Idx].Width = Nw;
        gWindows[Idx].Height = Nh;
        gWinBackupValid[Idx] = 0;

        DrawWindowAt(Idx);
        RepaintAfterResize(Idx);
        BackupWindowAt(Idx);
        RaiseWindow(Idx);
        GuiFocusApply();
    }

    CursorPaint();
    HalVideoPresent();
    GfxIrqLeave();
    ComposeEnd();

    gResizeBandOn = 0;
    gResizeBandW = 0;
    gResizeBandH = 0;
}
