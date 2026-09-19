/*
 * GuiDragBackup.c — 拖动备份缓冲与起始抓屏
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
            DebugWrite("Gui: drag snap OOM — cancel\n");
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
        DebugWrite("Gui: drag backup alloc failed\n");
        return;
    }
    HalVideoClearClip();
    for (i = 0; i < MAX_WINS; i++) {
        if (!gWindows[i].Active) {
            continue;
        }
        EnsureWindowBackupBuf(i);
        /*
         * ForceFull 只对「帧缓冲上可见」的窗：已被上层盖住时整窗 ReadRect
         * 会把前景烙进 gWinBackup，ClearOld 露底时出现拖动烙印。
         * 遮挡窗走非 ForceFull，重叠像素保留先前干净备份。
         */
        BackupWindowAtEx(i, !WindowOccludedByOtherOrShadow(i));
    }
    if (!gWinBackupValid[DragIdx]) {
        DebugWrite("Gui: drag backup invalid\n");
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
        DebugWrite("Gui: drag aborted (no backup)\n");
        ResetDragState();
        gDragWin = -1;
        ComposeEnd();
        return;
    }
    ComposeEnd();
}

