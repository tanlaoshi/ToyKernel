/*
 * GuiResizeHit.c — 改大小热区命中与光标外形
 */
#include "GuiPrivate.h"
#include "Desktop.h"

#define RESIZE_HOT 16u

int GuiResizeEdgeAt(const GUI_WINDOW *W, UINT32 X, UINT32 Y) {
    UINT32 Rx;
    UINT32 Ry;
    int OnRight;
    int OnBottom;

    if (!W || !W->Active || W->Width < RESIZE_HOT || W->Height < RESIZE_HOT) {
        return RESIZE_EDGE_NONE;
    }
    if (X < W->X || Y < W->Y || X >= W->X + W->Width || Y >= W->Y + W->Height) {
        return RESIZE_EDGE_NONE;
    }
    Rx = W->X + W->Width - RESIZE_HOT;
    Ry = W->Y + W->Height - RESIZE_HOT;
    OnRight = (X >= Rx) ? 1 : 0;
    OnBottom = (Y >= Ry) ? 1 : 0;
    if (OnRight && OnBottom) {
        return RESIZE_EDGE_SE;
    }
    if (OnRight) {
        return RESIZE_EDGE_E;
    }
    if (OnBottom) {
        return RESIZE_EDGE_S;
    }
    return RESIZE_EDGE_NONE;
}

int PointInResizeCorner(const GUI_WINDOW *W, UINT32 X, UINT32 Y) {
    return GuiResizeEdgeAt(W, X, Y) != RESIZE_EDGE_NONE ? 1 : 0;
}

int GuiResizeCursorKindAt(UINT32 X, UINT32 Y) {
    int Edge;
    int Top;

    if (gResizeWin >= 0) {
        Edge = gResizeEdge;
    } else {
        if (DesktopStartMenuIsOpen()) {
            return CURSOR_KIND_ARROW;
        }
        Top = TopWindowAt(X, Y);
        if (Top < 0) {
            return CURSOR_KIND_ARROW;
        }
        Edge = GuiResizeEdgeAt(&gWindows[Top], X, Y);
    }
    if (Edge == RESIZE_EDGE_SE) {
        return CURSOR_KIND_RESIZE_SE;
    }
    if (Edge == RESIZE_EDGE_E) {
        return CURSOR_KIND_RESIZE_E;
    }
    if (Edge == RESIZE_EDGE_S) {
        return CURSOR_KIND_RESIZE_S;
    }
    return CURSOR_KIND_ARROW;
}
