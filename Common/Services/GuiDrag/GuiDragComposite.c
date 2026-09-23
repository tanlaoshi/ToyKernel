/*
 * GuiDragComposite.c — 拖动脏区合成与脚印
 * 核心：GuiDrag.c
 */
#include "GuiPrivate.h"
#include "UI.h"
#include "HalVideo.h"
#include "Hal.h"
#include "Debug.h"
#include "PhysicalMemory.h"
#include "Desktop.h"
#include "Theme.h"
#include "SettingsUi.h"
#include "FilesUi.h"
#include "StoreUi.h"
#include "EditUi.h"


/*
 * 被拖窗下方可见像素：优先窗备份（含 Shell 文字）；无备份再解析空壳。
 * 注意：若备份是在被上层盖住时从 FB 抓的，重叠区可能脏——Capture 时用重绘采样避免。
 */
UINT32 TopmostBelowDragPixel(UINT32 Px, UINT32 Py, int DragIdx) {
    int i;
    UINT32 IconColor;

    for (i = DragIdx - 1; i >= 0; i--) {
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
    if (DesktopSamplePixel(Px, Py, &IconColor)) {
        return IconColor;
    }
    return DesktopBgAt(Px, Py);
}

/* 拖动脏区单像素合成：新位置用被拖窗备份；露出区域用拖动前快照/下方干净层 */
UINT32 CompositeDragPixel(UINT32 Px, UINT32 Py, int DragIdx,
                                 UINT32 Nx, UINT32 Ny, UINT32 Nw, UINT32 Nh) {
    int i;

    if (Px >= Nx && Px < Nx + Nw && Py >= Ny && Py < Ny + Nh &&
        gWinBackupValid[DragIdx] && gWinBackup[DragIdx] != 0) {
        UINT32 Bw = gWinBackupW[DragIdx];
        UINT32 Bh = gWinBackupH[DragIdx];
        UINT32 Lx = Px - Nx;
        UINT32 Ly = Py - Ny;

        if (Lx < Bw && Ly < Bh) {
            return gWinBackup[DragIdx][Ly * Bw + Lx];
        }
    }

    /* 起始 footprint：用 under-drag，勿用含拖动窗本体的 screen snap */
    if (gUnderDragValid &&
        Px >= gDragStartX && Px < gDragStartX + gDragStartW &&
        Py >= gDragStartY && Py < gDragStartY + gDragStartH) {
        UINT32 Ux = Px - gDragStartX;
        UINT32 Uy = Py - gDragStartY;

        return gUnderDrag[Uy * gDragStartW + Ux];
    }

    /* 拖动前全屏快照是露出区域的权威来源（被挡窗备份在重叠区是脏的） */
    if (gScreenSnapValid && Px < gScreenWidth && Py < gScreenHeight) {
        return gScreenSnap[Py * gScreenWidth + Px];
    }

    for (i = MAX_WINS - 1; i >= 0; i--) {
        if (i == DragIdx || !gWindows[i].Active) {
            continue;
        }
        if (WindowBackupCoversPixel(i, Px, Py)) {
            return SampleWindowBackupPixel(i, Px, Py);
        }
    }
    return TopmostBelowDragPixel(Px, Py, DragIdx);
}


/* 旧/新 footprint 并集一次扫描线写出，避免先清灰再全窗重贴的两步闪屏 */
void CompositeDragDirtyRegion(int DragIdx, UINT32 OldX, UINT32 OldY,
                                     UINT32 Ww, UINT32 Wh) {
    const GUI_WINDOW *Drag = &gWindows[DragIdx];
    UINT32 Nx = Drag->X;
    UINT32 Ny = Drag->Y;
    UINT32 DuX;
    UINT32 DuY;
    UINT32 DuW;
    UINT32 DuH;
    UINT32 Right;
    UINT32 Bottom;
    UINT32 Row;

    if (Ww == 0 || Wh == 0) {
        return;
    }
    DuX = OldX < Nx ? OldX : Nx;
    DuY = OldY < Ny ? OldY : Ny;
    Right = OldX + Ww;
    if (Nx + Ww > Right) {
        Right = Nx + Ww;
    }
    Bottom = OldY + Wh;
    if (Ny + Wh > Bottom) {
        Bottom = Ny + Wh;
    }
    DuW = Right - DuX;
    DuH = Bottom - DuY;
    if (DuX > DRAG_BORDER_PAD) {
        DuX -= DRAG_BORDER_PAD;
        DuW += DRAG_BORDER_PAD;
    }
    if (DuY > DRAG_BORDER_PAD) {
        DuY -= DRAG_BORDER_PAD;
        DuH += DRAG_BORDER_PAD;
    }
    DuW += DRAG_BORDER_PAD;
    DuH += DRAG_BORDER_PAD;
    ClipRectToScreen(&DuX, &DuY, &DuW, &DuH);
    if (DuW == 0 || DuH == 0) {
        return;
    }
    if (EnsureDragDirtyBuf(DuW, DuH)) {
        for (Row = 0; Row < DuH; Row++) {
            UINT32 Py = DuY + Row;
            UINT32 Col;

            for (Col = 0; Col < DuW; Col++) {
                UINT32 Px = DuX + Col;

                gDragDirty[Row * DuW + Col] =
                    CompositeDragPixel(Px, Py, DragIdx, Nx, Ny, Ww, Wh);
            }
        }
        /* Present：短临界区写后缓冲再提交到 GOP（PR-G9） */
        HalVideoWriteRect(DuX, DuY, DuW, DuH, gDragDirty);
        GuiDragFrameAccount();
        HalVideoPresent();
        return;
    }
    /* 离屏缓冲不足：按 DRAG_ROW_MAX 横向分块写，禁止静默截断右侧 */
    {
        UINT32 Col0;

        for (Col0 = 0; Col0 < DuW; Col0 += DRAG_ROW_MAX) {
            UINT32 ChunkW = DuW - Col0;

            if (ChunkW > DRAG_ROW_MAX) {
                ChunkW = DRAG_ROW_MAX;
            }
            for (Row = 0; Row < DuH; Row++) {
                UINT32 Py = DuY + Row;
                UINT32 Col;

                for (Col = 0; Col < ChunkW; Col++) {
                    UINT32 Px = DuX + Col0 + Col;

                    gDragRowBuf[Col] =
                        CompositeDragPixel(Px, Py, DragIdx, Nx, Ny, Ww, Wh);
                }
                HalVideoWriteRect(DuX + Col0, Py, ChunkW, 1, gDragRowBuf);
            }
        }
        GuiDragFrameAccount();
        HalVideoPresent();
    }
}


void RestoreWindowsInFootprint(UINT32 Fx, UINT32 Fy, UINT32 Fw, UINT32 Fh,
                                      int SkipIdx) {
    int i;
    UINT32 N = ThemeWindowShadowSize();

    for (i = 0; i < MAX_WINS; i++) {
        if (!gWindows[i].Active || i == SkipIdx) {
            continue;
        }
        /* 含影扩边：足迹只擦到邻窗阴影时也要贴回本体，否则影被桌面清掉后不补 */
        if (RectIntersects(gWindows[i].X, gWindows[i].Y,
                           gWindows[i].Width + N, gWindows[i].Height + N,
                           Fx, Fy, Fw, Fh)) {
            PaintWindowFromBackup(i);
        }
    }
}


/* 清除整片旧 footprint：先恢复被盖住的其它窗，再填桌面色与图标（避让窗口） */
void ClearOldDragFootprint(UINT32 Ox, UINT32 Oy, UINT32 Ww, UINT32 Wh,
                                  int DragIdx) {
    int i;
    UINT32 N = ThemeWindowShadowSize();

    RestoreWindowsInFootprint(Ox, Oy, Ww, Wh, DragIdx);
    FillDesktopRectClipped(Ox, Oy, Ww, Wh);
    DesktopDrawRect(Ox, Oy, Ww, Wh);
    /* 相交他窗阴影（含影扩边相交；被拖窗阴影由调用方在贴窗后画） */
    for (i = 0; i < MAX_WINS; i++) {
        if (!gWindows[i].Active || i == DragIdx) {
            continue;
        }
        if (RectIntersects(gWindows[i].X, gWindows[i].Y,
                           gWindows[i].Width + N, gWindows[i].Height + N,
                           Ox, Oy, Ww, Wh)) {
            DrawWindowShadowAt(i);
        }
    }
}

/*
 * 图标拖动置顶：脚印内可能盖住窗/影，须整块壁纸擦除再贴备份+阴影。
 * Desktop 经 DesktopSetClearIconFootprint 回调，不 include Gui。
 */
void GuiClearIconDragFootprint(UINT32 X, UINT32 Y, UINT32 W, UINT32 H) {
    int i;
    UINT32 N = ThemeWindowShadowSize();

    if (W == 0 || H == 0) {
        return;
    }
    HalVideoClearClip();
    DesktopFillRect(X, Y, W, H);
    RestoreWindowsInFootprint(X, Y, W, H, -1);
    DesktopDrawRect(X, Y, W, H);
    for (i = 0; i < MAX_WINS; i++) {
        if (!gWindows[i].Active) {
            continue;
        }
        if (RectIntersects(gWindows[i].X, gWindows[i].Y,
                           gWindows[i].Width + N, gWindows[i].Height + N,
                           X, Y, W, H)) {
            DrawWindowShadowAt(i);
        }
    }
}


/* 按 z 序重画全部窗口（被拖窗最后画；备份不可用时回退） */
void PaintAllWindowsDraw(int DragIdx) {
    int i;

    for (i = 0; i < MAX_WINS; i++) {
        if (!gWindows[i].Active || i == DragIdx) {
            continue;
        }
        DrawWindowAt(i);
    }
    if (DragIdx >= 0 && DragIdx < MAX_WINS && gWindows[DragIdx].Active) {
        DrawWindowAt(DragIdx);
    }
}


/* 拖动一帧：擦旧足迹 + 贴窗备份 + Present（不用全屏 snap 合成，避残影/OOM） */
void RedrawDragFrame(int DragIdx, UINT32 OldX, UINT32 OldY) {
    const GUI_WINDOW *Drag = &gWindows[DragIdx];
    UINT32 Ww = Drag->Width;
    UINT32 Wh = Drag->Height;
    UINT32 Fx = OldX;
    UINT32 Fy = OldY;
    UINT32 Fw = Ww;
    UINT32 Fh = Wh;

    HalVideoClearClip();
    ExpandRectByWindowShadow(&Fx, &Fy, &Fw, &Fh);
    ClipRectToScreen(&Fx, &Fy, &Fw, &Fh);
    ClearOldDragFootprint(Fx, Fy, Fw, Fh, DragIdx);
    if (gWinBackupValid[DragIdx] && gWinBackup[DragIdx] != 0) {
        PaintWindowFromBackup(DragIdx);
        DrawWindowShadowAt(DragIdx);
    } else {
        DrawWindowAt(DragIdx);
    }
    /* 条带间开中断；勿整脏区一次 cli（与 Slide 相同） */
    GuiDragFrameAccount();
    HalVideoPresent();
}

