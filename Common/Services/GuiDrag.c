/*
 * GuiDrag.c — PR-R2：标题栏拖动与脏区合成
 */
#include "GuiPriv.h"
#include "UI.h"
#include "HalVideo.h"
#include "Hal.h"
#include "Debug.h"
#include "PhysicalMemory.h"
#include "Desktop.h"
#include "SettingsUi.h"
#include "FilesUi.h"
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


int EnsureDragDirtyBuf(UINT32 Ww, UINT32 Hh) {
    UINT64 Need;
    UINT32 Pages;

    if (Ww == 0 || Hh == 0) {
        return 0;
    }
    Need = (UINT64)Ww * (UINT64)Hh;
    if (Need > 0xFFFFFFFFu / sizeof(UINT32)) {
        return 0;
    }
    if (gDragDirty != 0 && gDragDirtyCap >= (UINT32)Need) {
        return 1;
    }
    Pages = BackupPageCount(Ww, Hh);
    if (Pages == 0) {
        return 0;
    }
    if (gDragDirty != 0) {
        PhysicalMemoryFreePages(gDragDirty, gDragDirtyPages);
        gDragDirty = 0;
        gDragDirtyPages = 0;
        gDragDirtyCap = 0;
    }
    gDragDirty = (UINT32 *)PhysicalMemoryAllocatePages(Pages);
    if (gDragDirty == 0) {
        return 0;
    }
    gDragDirtyPages = Pages;
    gDragDirtyCap = (UINT32)(((UINT64)Pages * PAGE_SIZE) / sizeof(UINT32));
    return 1;
}


int EnsureUnderDragBuf(UINT32 Ww, UINT32 Wh) {
    UINT32 Pages = BackupPageCount(Ww, Wh);

    if (Pages == 0) {
        return 0;
    }
    if (gUnderDrag != 0 && gUnderDragPages == Pages) {
        return 1;
    }
    if (gUnderDrag != 0) {
        PhysicalMemoryFreePages(gUnderDrag, gUnderDragPages);
        gUnderDrag = 0;
        gUnderDragPages = 0;
    }
    gUnderDragValid = 0;
    gUnderDrag = (UINT32 *)PhysicalMemoryAllocatePages(Pages);
    if (gUnderDrag == 0) {
        return 0;
    }
    gUnderDragPages = Pages;
    return 1;
}


void CaptureDragRestoreData(int DragIdx) {
    UINT32 Pages;
    int i;

    gScreenSnapValid = 0;
    gUnderDragValid = 0;
    if (DragIdx < 0 || DragIdx >= MAX_WINS || !gWindows[DragIdx].Active) {
        return;
    }
    if (gScreenWidth == 0 || gScreenHeight == 0) {
        return;
    }

    gDragStartX = gWindows[DragIdx].X;
    gDragStartY = gWindows[DragIdx].Y;
    gDragStartW = gWindows[DragIdx].Width;
    gDragStartH = gWindows[DragIdx].Height;
    if (gDragStartW == 0 || gDragStartH == 0) {
        return;
    }

    Pages = BackupPageCount(gScreenWidth, gScreenHeight);
    if (gScreenSnap != 0 && gScreenSnapPages != Pages) {
        PhysicalMemoryFreePages(gScreenSnap, gScreenSnapPages);
        gScreenSnap = 0;
        gScreenSnapPages = 0;
    }
    if (gScreenSnap == 0) {
        gScreenSnap = (UINT32 *)PhysicalMemoryAllocatePages(Pages);
        if (gScreenSnap == 0) {
            DebugWrite("gui: drag snap OOM — cancel\n");
            gScreenSnapValid = 0;
            return;
        }
        gScreenSnapPages = Pages;
    }
    /* 含被拖窗的全屏快照：非起始 footprint 露底时用 */
    HalVideoReadRect(0, 0, gScreenWidth, gScreenHeight, gScreenSnap);
    gScreenSnapValid = 1;
    EnsureDragDirtyBuf(gScreenWidth, gScreenHeight);

    if (!EnsureUnderDragBuf(gDragStartW, gDragStartH)) {
        return;
    }

    /*
     * 起始 footprint 的「去被拖窗」场景：暂时取消被拖窗 Active，重画桌面+其它窗，
     * 再读入 under-drag，最后恢复 Active 并贴回被拖窗。
     */
    HalVideoClearClip();
    gWindows[DragIdx].Active = 0;
    DesktopFillRect(gDragStartX, gDragStartY, gDragStartW, gDragStartH);
    DesktopDrawRect(gDragStartX, gDragStartY, gDragStartW, gDragStartH);
    for (i = 0; i < MAX_WINS; i++) {
        if (!gWindows[i].Active || i == DragIdx) {
            continue;
        }
        if (RectIntersects(gWindows[i].X, gWindows[i].Y, gWindows[i].Width, gWindows[i].Height,
                           gDragStartX, gDragStartY, gDragStartW, gDragStartH)) {
            /* 必须贴备份（含客户区文字），禁止 DrawWindowAt 空壳 */
            PaintWindowFromBackup(i);
        }
    }
    HalVideoReadRect(gDragStartX, gDragStartY, gDragStartW, gDragStartH, gUnderDrag);
    gUnderDragValid = 1;
    gWindows[DragIdx].Active = 1;

    /* 贴回被拖窗（备份应在 Begin/Start 里已抓好） */
    if (gWinBackupValid[DragIdx] && gWinBackup[DragIdx] != 0) {
        HalVideoWriteRect(gDragStartX, gDragStartY, gWinBackupW[DragIdx],
                          gWinBackupH[DragIdx], gWinBackup[DragIdx]);
        DrawWindowChromeAt(DragIdx);
    } else {
        DrawWindowAt(DragIdx);
    }

    /* 相交窗已从备份恢复，无需再 BackupWindowAt（以免读到瞬时脏 FB） */
}


void BeginDragBackups(int DragIdx) {
    int i;

    /*
     * 不用全屏 snap/under 合成：省 2× 帧缓冲，避免 OOM 半状态；
     * 也避免 snap 含窗本体时 Composite 露底失败留下标题栏/客户区残影
     * （见 44b1633；bbfa299 回潮后再现）。
     */
    ResetDragState();
    if (DragIdx < 0 || DragIdx >= MAX_WINS || !gWindows[DragIdx].Active) {
        return;
    }
    if (!EnsureWindowBackupBuf(DragIdx)) {
        DebugWrite("gui: drag backup alloc failed\n");
        return;
    }
    HalVideoClearClip();
    for (i = 0; i < MAX_WINS; i++) {
        if (!gWindows[i].Active) {
            continue;
        }
        EnsureWindowBackupBuf(i);
        /* ForceFull：重叠区也要完整备份，ClearOld 才能正确露底 */
        BackupWindowAtEx(i, 1);
    }
    if (!gWinBackupValid[DragIdx]) {
        DebugWrite("gui: drag backup invalid\n");
        return;
    }
    gDragHasBackup = 1;
}


void StartDragBackups(int DragIdx) {
    /* G7：抓屏/合成不关中断，只锁光标擦除；ComposeBusy 防嵌套鼠标 */
    ComposeBegin();
    GfxIrqEnter();
    CursorRestore();
    HalVideoPresent();
    GfxIrqLeave();
    GuiFocusSave();
    BeginDragBackups(DragIdx);
    if (!gDragHasBackup || !gWinBackupValid[DragIdx]) {
        DebugWrite("gui: drag aborted (no backup)\n");
        ResetDragState();
        gDragWin = -1;
        ComposeEnd();
        return;
    }
    ComposeEnd();
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
        GfxIrqEnter();
        HalVideoWriteRect(DuX, DuY, DuW, DuH, gDragDirty);
        HalVideoPresent();
        GfxIrqLeave();
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
        GfxIrqEnter();
        HalVideoPresent();
        GfxIrqLeave();
    }
}


void RestoreWindowsInFootprint(UINT32 Fx, UINT32 Fy, UINT32 Fw, UINT32 Fh,
                                      int SkipIdx) {
    int i;

    for (i = 0; i < MAX_WINS; i++) {
        if (!gWindows[i].Active || i == SkipIdx) {
            continue;
        }
        if (RectIntersects(gWindows[i].X, gWindows[i].Y, gWindows[i].Width, gWindows[i].Height,
                           Fx, Fy, Fw, Fh)) {
            PaintWindowFromBackup(i);
        }
    }
}


/* 清除整片旧 footprint：先恢复被盖住的其它窗，再填桌面色与图标（避让窗口） */
void ClearOldDragFootprint(UINT32 Ox, UINT32 Oy, UINT32 Ww, UINT32 Wh,
                                  int DragIdx) {
    RestoreWindowsInFootprint(Ox, Oy, Ww, Wh, DragIdx);
    FillDesktopRectClipped(Ox, Oy, Ww, Wh);
    DesktopDrawRect(Ox, Oy, Ww, Wh);
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

    HalVideoClearClip();
    ClearOldDragFootprint(OldX, OldY, Ww, Wh, DragIdx);
    if (gWinBackupValid[DragIdx] && gWinBackup[DragIdx] != 0) {
        PaintWindowFromBackup(DragIdx);
    } else {
        DrawWindowAt(DragIdx);
    }
    GfxIrqEnter();
    HalVideoPresent();
    GfxIrqLeave();
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
        HalVideoPresent();
    }
    GfxIrqLeave();
    if (gDragHasBackup) {
        W->X = NewX;
        W->Y = NewY;
        RedrawDragFrame(Idx, Ox, Oy);
        /* RedrawDragFrame 内已 Present */
    } else if (gDragWin >= 0) {
        W->X = NewX;
        W->Y = NewY;
        HalVideoClearClip();
        ClearOldDragFootprint(Ox, Oy, Ww, Wh, Idx);
        if (gWinBackupValid[Idx]) {
            PaintWindowFromBackup(Idx);
        } else {
            PaintAllWindowsDraw(Idx);
        }
        GfxIrqEnter();
        HalVideoPresent();
        GfxIrqLeave();
    } else {
        HalVideoCopyRect(Ox, Oy, NewX, NewY, Ww, Wh);
        W->X = NewX;
        W->Y = NewY;
        ClearOldDragFootprint(Ox, Oy, Ww, Wh, Idx);
        RefreshOtherChrome(Idx);
        DrawWindowChromeAt(Idx);
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
        }
        if (gWindows[DragIdx].Kind == GUI_WIN_SETTINGS) {
            SettingsUiRepaint();
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

