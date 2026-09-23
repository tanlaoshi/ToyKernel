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
     * 后缓冲已是这一帧终稿。勿 GfxIrqEnter：它会把 Present 整段关中断，
     * 64 行条带间的开中断失效，大窗一次 cli 就是拖窗顿挫。
     * 也不要把条带抬成整脏区：左右条和窗体分块刷，避免两侧整块闪。
     */
    GuiDragFrameAccount();
    HalVideoPresent();
}
