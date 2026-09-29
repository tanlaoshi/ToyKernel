/*
 * GuiResize.c — 右/底边与右下角拖拽改大小生命周期（PR-S3-guiresize-1）
 *
 * 线框/尺寸见 GuiResizeBand.c；热区见 GuiResizeHit.c。
 */
#include "GuiPrivate.h"
#include "UI.h"
#include "HalVideo.h"
#include "Desktop.h"

#define RESIZE_MIN_STEP 3u

int    gResizeWin = -1;
int    gResizeArmed;
int    gResizeEdge;
UINT32 gResizeOrigW;
UINT32 gResizeOrigH;
INT32  gResizeAnchorX;
INT32  gResizeAnchorY;
int    gResizeBandOn;
UINT32 gResizeBandW;
UINT32 gResizeBandH;

void GuiResizeBegin(int Idx, UINT32 X, UINT32 Y) {
    int Edge;

    if (Idx < 0 || Idx >= MAX_WINS || !gWindows[Idx].Active) {
        return;
    }
    Edge = GuiResizeEdgeAt(&gWindows[Idx], X, Y);
    if (Edge == RESIZE_EDGE_NONE) {
        return;
    }
    gDragWin = -1;
    gDragArmed = 0;
    gResizeWin = Idx;
    gResizeEdge = Edge;
    gResizeOrigW = gWindows[Idx].Width;
    gResizeOrigH = gWindows[Idx].Height;
    gResizeAnchorX = (INT32)X;
    gResizeAnchorY = (INT32)Y;
    gResizeArmed = 1;
    gResizeBandOn = 0;
    gResizeBandW = 0;
    gResizeBandH = 0;
    gCursorKind = GuiResizeCursorKindAt(X, Y);
}

void GuiResizeUpdate(UINT32 X, UINT32 Y) {
    UINT32 Nw;
    UINT32 Nh;
    UINT32 OdW;
    UINT32 OdH;

    if (gResizeWin < 0 || gResizeWin >= MAX_WINS || !gWindows[gResizeWin].Active) {
        return;
    }
    ComputeResizeSize(gResizeWin, gResizeEdge, X, Y, &Nw, &Nh);
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
    int Edge = gResizeEdge;
    UINT32 Nw;
    UINT32 Nh;
    int Did;

    if (Idx < 0 || Idx >= MAX_WINS || !gWindows[Idx].Active) {
        gResizeWin = -1;
        gResizeArmed = 0;
        gResizeEdge = RESIZE_EDGE_NONE;
        gResizeBandOn = 0;
        return;
    }

    Did = gResizeBandOn ||
          (gResizeBandW != 0 && gResizeBandH != 0);
    /* 须在清 gResizeEdge / gResizeWin 之前算尺寸 */
    ComputeResizeSize(Idx, Edge, gCursorX, gCursorY, &Nw, &Nh);

    gResizeWin = -1;
    gResizeArmed = 0;
    gResizeEdge = RESIZE_EDGE_NONE;

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

        ExpandRectByWindowShadow(&Fx, &Fy, &Fw, &Fh);
        ClipRectToScreen(&Fx, &Fy, &Fw, &Fh);

        /*
         * 必须先改成新尺寸再 Clear：FillDesktopRectClipped 认
         * PointInAnyActiveWindow；若仍是旧 W/H，向左/上缩时露出条
         * 会被当成「仍在窗内」而跳过 → 旧客户区像素残留。
         */
        gWindows[Idx].Width = Nw;
        gWindows[Idx].Height = Nh;
        gWinBackupValid[Idx] = 0;

        ClearOldDragFootprint(Fx, Fy, Fw, Fh, Idx);

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
