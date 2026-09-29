/*
 * GuiDragMove.c — 窗位夹紧与拖移重画（PR-S3-guidrag-1）
 *
 * 从 GuiDrag.c 原样搬家；不改语义。
 */
#include "GuiPrivate.h"
#include "UI.h"
#include "HalVideo.h"
#include "Desktop.h"

/* 窗口必须完整留在屏内 */
void ClampWindowPos(const GUI_WINDOW *W, INT32 *X, INT32 *Y) {
    INT32 MaxX = (INT32)gScreenWidth - (INT32)W->Width;
    INT32 MaxY = (INT32)gScreenHeight - (INT32)W->Height;

    if (MaxX < 0) {
        MaxX = 0;
    }
    if (MaxY < 0) {
        MaxY = 0;
    }
    if (*X < 0) {
        *X = 0;
    }
    if (*Y < 0) {
        *Y = 0;
    }
    if (*X > MaxX) {
        *X = MaxX;
    }
    if (*Y > MaxY) {
        *Y = MaxY;
    }
}

void MoveWindowTo(int Idx, UINT32 NewX, UINT32 NewY) {
    GUI_WINDOW *W;
    UINT32 Ox;
    UINT32 Oy;
    UINT32 Ww;
    UINT32 Wh;

    if (Idx < 0 || Idx >= MAX_WINS) {
        return;
    }
    W = &gWindows[Idx];
    if (!W->Active) {
        return;
    }
    Ox = W->X;
    Oy = W->Y;
    Ww = W->Width;
    Wh = W->Height;
    if (NewX == Ox && NewY == Oy) {
        return;
    }
    if (NewX + Ww > gScreenWidth || NewY + Wh > gScreenHeight) {
        return;
    }

    /* G7：合成开中断；仅光标 erase/paint 与 Present 进 GfxIrq */
    ComposeBegin();
    GfxIrqEnter();
    if (gCursorVisible) {
        CursorRestore();
        /* 拖窗中勿在此 Present：会多刷一帧旧画面，加重两侧闪 */
        if (gDragWin < 0) {
            HalVideoPresent();
        }
    }
    GfxIrqLeave();
    if (gDragHasBackup) {
        W->X = NewX;
        W->Y = NewY;
        RedrawDragFrameSlide(Idx, Ox, Oy);
    } else if (gDragWin >= 0) {
        UINT32 Fx = Ox;
        UINT32 Fy = Oy;
        UINT32 Fw = Ww;
        UINT32 Fh = Wh;

        W->X = NewX;
        W->Y = NewY;
        HalVideoClearClip();
        ExpandRectByWindowShadow(&Fx, &Fy, &Fw, &Fh);
        ClipRectToScreen(&Fx, &Fy, &Fw, &Fh);
        ClearOldDragFootprint(Fx, Fy, Fw, Fh, Idx);
        if (gWinBackupValid[Idx]) {
            PaintWindowFromBackup(Idx);
            DrawWindowShadowAt(Idx);
        } else {
            PaintAllWindowsDraw(Idx);
        }
        GuiDragFrameAccount();
        HalVideoPresent();
    } else {
        UINT32 Fx = Ox;
        UINT32 Fy = Oy;
        UINT32 Fw = Ww;
        UINT32 Fh = Wh;

        HalVideoCopyRect(Ox, Oy, NewX, NewY, Ww, Wh);
        W->X = NewX;
        W->Y = NewY;
        ExpandRectByWindowShadow(&Fx, &Fy, &Fw, &Fh);
        ClipRectToScreen(&Fx, &Fy, &Fw, &Fh);
        ClearOldDragFootprint(Fx, Fy, Fw, Fh, Idx);
        RefreshOtherChrome(Idx);
        DrawWindowChromeAt(Idx);
        DrawWindowShadowAt(Idx);
    }
    if (gDragWin < 0) {
        GfxIrqEnter();
        CursorPaint();
        HalVideoPresent();
        GfxIrqLeave();
    }
    ComposeEnd();
}
