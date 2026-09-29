/*
 * GuiDragSlide.c — CopyRect 平移 + 只擦露出条（减左右拖频闪）
 * 核心：GuiDrag.c
 */
#include "GuiPrivate.h"
#include "HalVideo.h"
#include "Hal.h"
#include "Theme.h"
#include "Debug.h"

/* 一帧只许一个核改后缓冲并 Present；另一核见忙则排空鼠标 */
static volatile UINT32 gDragFrame;
static UINT32 gDragFrames;

int GuiDragFrameTry(void) {
    return __sync_lock_test_and_set(&gDragFrame, 1u) == 0;
}

void GuiDragFrameLeave(void) {
    __sync_lock_release(&gDragFrame);
}

int GuiPresentBlocked(void) {
    return gDragFrame != 0 || gComposeBusy != 0;
}

int GuiDragActive(void) {
    return gDragWin >= 0 || gResizeWin >= 0;
}

void GuiDragFrameAccount(void) {
    gDragFrames++;
}

void GuiDragFrameLog(void) {
    if (gDragFrames == 0) {
        return;
    }
    DebugWrite("Gui: drag frames ");
    DebugHex32(gDragFrames);
    DebugWrite("\n");
    gDragFrames = 0;
}

/*
 * 露出条勿 ExpandRectByWindowShadow：影向右下扩，会吃进已 CopyRect 的新窗左边 → 花块。
 * 旧影由随后 DrawWindowShadowAt / 桌面擦条覆盖。
 */
static void ClearExposedStrip(UINT32 X, UINT32 Y, UINT32 W, UINT32 H, int DragIdx) {
    UINT32 Fx = X;
    UINT32 Fy = Y;
    UINT32 Fw = W;
    UINT32 Fh = H;

    if (W == 0 || H == 0) {
        return;
    }
    ClipRectToScreen(&Fx, &Fy, &Fw, &Fh);
    ClearOldDragFootprint(Fx, Fy, Fw, Fh, DragIdx);
}

void RedrawDragFrameSlide(int DragIdx, UINT32 OldX, UINT32 OldY) {
    GUI_WINDOW *Drag;
    UINT32 Nx;
    UINT32 Ny;
    UINT32 Ww;
    UINT32 Wh;
    INT32 Dx;
    INT32 Dy;

    if (DragIdx < 0 || DragIdx >= MAX_WINS) {
        return;
    }
    Drag = &gWindows[DragIdx];
    Nx = Drag->X;
    Ny = Drag->Y;
    Ww = Drag->Width;
    Wh = Drag->Height;
    Dx = (INT32)Nx - (INT32)OldX;
    Dy = (INT32)Ny - (INT32)OldY;

    HalVideoClearClip();
    HalVideoCopyRect(OldX, OldY, Nx, Ny, Ww, Wh);

    if (Dx > 0) {
        ClearExposedStrip(OldX, OldY, (UINT32)Dx, Wh, DragIdx);
    } else if (Dx < 0) {
        ClearExposedStrip(Nx + Ww, OldY, (UINT32)(-Dx), Wh, DragIdx);
    }
    if (Dy > 0) {
        ClearExposedStrip(OldX, OldY, Ww, (UINT32)Dy, DragIdx);
    } else if (Dy < 0) {
        ClearExposedStrip(OldX, Ny + Wh, Ww, (UINT32)(-Dy), DragIdx);
    }

    /* Copy/擦条偶发踩窗边：用备份重贴保证客户区干净 */
    if (gWinBackupValid[DragIdx] && gWinBackup[DragIdx] != 0) {
        PaintWindowFromBackup(DragIdx);
    }
    DrawWindowShadowAt(DragIdx);

    /*
     * igpu-5：旧∪新（含右下影）并成一块脏区再 Present。
     * GPU 路径一次 SRC_COPY；CPU 回退时抬高条带，减轻左右细闪。
     */
    {
        UINT32 Ux = (OldX < Nx) ? OldX : Nx;
        UINT32 Uy = (OldY < Ny) ? OldY : Ny;
        UINT32 Ur = (OldX + Ww > Nx + Ww) ? (OldX + Ww) : (Nx + Ww);
        UINT32 Ub = (OldY + Wh > Ny + Wh) ? (OldY + Wh) : (Ny + Wh);
        UINT32 Shadow = ThemeWindowShadowSize();

        if (Ur + Shadow < gScreenWidth) {
            Ur += Shadow;
        } else {
            Ur = gScreenWidth;
        }
        if (Ub + Shadow < gScreenHeight) {
            Ub += Shadow;
        } else {
            Ub = gScreenHeight;
        }
        if (Ur > Ux && Ub > Uy) {
            HalVideoMarkDirty(Ux, Uy, Ur - Ux, Ub - Uy);
        }
        /* 整块 Present：GPU 翻页或 CPU 一次性 memcpy，避免 64 行条带 */
        HalVideoSetPresentChunkRows(16384u);
    }
    GuiDragFrameAccount();
    HalVideoPresent();
    HalVideoSetPresentChunkRows(0);
}
