/*
 * GuiDrag.c — 标题栏拖动生命周期（核心）
 * 辅助：GuiDragBackup.c / GuiDragComposite.c
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
#include "DevicesUi.h"
#include "EditUi.h"


void ResetDragState(void) {
    /*
     * 只清拖动合成用的 snap/under/dirty。
     * 绝不能清 gWinBackupValid：单击标题栏也会进 GuiDragEnd，若无重叠会走这里，
     * 清掉后备份后 SyncWindowVisuals 只能 DrawWindowAt，Settings/Shell 文字全没。
     */
    if (gScreenSnap != 0) {
        PhysicalMemoryFreePages(gScreenSnap, gScreenSnapPages);
        gScreenSnap = 0;
        gScreenSnapPages = 0;
    }
    if (gUnderDrag != 0) {
        PhysicalMemoryFreePages(gUnderDrag, gUnderDragPages);
        gUnderDrag = 0;
        gUnderDragPages = 0;
    }
    if (gDragDirty != 0) {
        PhysicalMemoryFreePages(gDragDirty, gDragDirtyPages);
        gDragDirty = 0;
        gDragDirtyPages = 0;
        gDragDirtyCap = 0;
    }
    gScreenSnapValid = 0;
    gUnderDragValid = 0;
    gDragStartW = 0;
    gDragStartH = 0;
    gDragHasBackup = 0;
}


int AnyWindowsOverlap(void) {
    int i;
    int j;

    for (i = 0; i < MAX_WINS; i++) {
        if (!gWindows[i].Active) {
            continue;
        }
        for (j = i + 1; j < MAX_WINS; j++) {
            if (!gWindows[j].Active) {
                continue;
            }
            if (RectIntersects(gWindows[i].X, gWindows[i].Y, gWindows[i].Width, gWindows[i].Height,
                               gWindows[j].X, gWindows[j].Y, gWindows[j].Width, gWindows[j].Height)) {
                return 1;
            }
        }
    }
    return 0;
}

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
        GfxIrqEnter();
        HalVideoSetPresentChunkRows(0xFFFFFFFFu);
        HalVideoPresent();
        HalVideoSetPresentChunkRows(0);
        GfxIrqLeave();
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


void GuiDragUpdate(UINT32 X, UINT32 Y) {
    INT32 Nx;
    INT32 Ny;
    INT32 Dx;
    INT32 Dy;
    GUI_WINDOW *W;

    if (gDragWin < 0 || gDragWin >= MAX_WINS || !gWindows[gDragWin].Active) {
        return;
    }
    W = &gWindows[gDragWin];
    Nx = (INT32)X - gDragOffX;
    Ny = (INT32)Y - gDragOffY;
    ClampWindowPos(W, &Nx, &Ny);

    Dx = Nx - (INT32)W->X;
    Dy = Ny - (INT32)W->Y;
    if (Dx < 0) {
        Dx = -Dx;
    }
    if (Dy < 0) {
        Dy = -Dy;
    }
    if ((UINT32)Dx < DRAG_MIN_STEP && (UINT32)Dy < DRAG_MIN_STEP) {
        return;
    }
    if (gDragArmed) {
        StartDragBackups(gDragWin);
        gDragArmed = 0;
        if (gDragWin < 0 || !gDragHasBackup) {
            return;
        }
    }
    MoveWindowTo(gDragWin, (UINT32)Nx, (UINT32)Ny);
}


void GuiDragEnd(void) {
    int DragIdx = gDragWin;
    int DidDrag = gDragHasBackup;

    gDragWin = -1;
    gDragArmed = 0;
    /* 与 GuiDragUpdate 一致：-1 哨兵 + 上界，避免 -Warray-bounds */
    if (DragIdx >= 0 && DragIdx < MAX_WINS && gWindows[DragIdx].Active) {
        if (DidDrag) {
            INT32 Nx = (INT32)gCursorX - gDragOffX;
            INT32 Ny = (INT32)gCursorY - gDragOffY;

            ClampWindowPos(&gWindows[DragIdx], &Nx, &Ny);
            MoveWindowTo(DragIdx, (UINT32)Nx, (UINT32)Ny);
            RaiseWindow(DragIdx);
            /*
             * 残影：拖动路径上旧 chrome 落在「当前窗矩形之外」，只贴窗擦不掉。
             * 先铺桌面再按备份贴回，清轨迹；空色块若已烙进备份则随后 Settings/Shell 重画补。
             */
            SyncWindowVisualsEx(1);
            GuiFocusApply();
            /*
             * Sync 会把上层阴影画到下层露出区（正确）。下层备份保持干净：
             * 被挡像素不吸入上层；移走后下次 Sync/ClearOld 用备份重贴即无烙印。
             */
        }
        if (gWindows[DragIdx].Kind == GUI_WIN_SETTINGS) {
            SettingsUiRepaint();
        } else if (gWindows[DragIdx].Kind == GUI_WIN_STORE) {
            StoreUiRepaint();
        } else if (gWindows[DragIdx].Kind == GUI_WIN_DEVICES) {
            DevicesUiRepaint();
        } else if (gWindows[DragIdx].Kind == GUI_WIN_FILES) {
            FilesUiRepaint();
        } else if (gWindows[DragIdx].Kind == GUI_WIN_EDIT) {
            EditUiRepaint();
        } else if (gWindows[DragIdx].Kind == GUI_WIN_USER) {
            PaintUserClient(DragIdx);
        } else if (gWindows[DragIdx].Kind == GUI_WIN_SHELL &&
                   !gWinBackupValid[DragIdx]) {
            GuiConsoleOpsOnShellOpened();
        }
        /* 清桌面合成后：无备份的 Shell 客户区是空壳，补画控制台 */
        if (DidDrag) {
            int i;

            for (i = 0; i < MAX_WINS; i++) {
                if (gWindows[i].Active && gWindows[i].Kind == GUI_WIN_SHELL &&
                    !gWinBackupValid[i]) {
                    int Prev = gFocusWin;

                    gFocusWin = i;
                    GuiConsoleOpsOnShellOpened();
                    gFocusWin = Prev;
                }
            }
        }
        if (gWindows[DragIdx].Active) {
            BackupWindowAt(DragIdx);
        }
    }
    if (!AnyWindowsOverlap()) {
        ResetDragState();
    }
}

