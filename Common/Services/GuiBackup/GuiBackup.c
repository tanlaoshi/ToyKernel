/*
 * GuiBackup.c — 窗备份缓冲（核心）
 * 辅助：GuiBackupSample.c
 *
 * 从 GuiBackup.c 单体迁出；只搬家、不改逻辑。
 */
#include "GuiPrivate.h"
#include "UI.h"
#include "HalVideo.h"
#include "Hal.h"
#include "PhysicalMemory.h"
#include "Theme.h"
#include "Font.h"
#include "Desktop.h"

int AllActiveWindowsHaveValidBackup(void) {
    int i;

    for (i = 0; i < MAX_WINS; i++) {
        if (gWindows[i].Active && !gWinBackupValid[i]) {
            return 0;
        }
    }
    return 1;
}


UINT32 BackupPageCount(UINT32 Ww, UINT32 Wh) {
    UINT64 Bytes = (UINT64)Ww * (UINT64)Wh * sizeof(UINT32);

    return (UINT32)((Bytes + PAGE_SIZE - 1) / PAGE_SIZE);
}


int EnsureWindowBackupBuf(int Idx) {
    UINT32 Pages;

    if (Idx < 0 || Idx >= MAX_WINS || !gWindows[Idx].Active) {
        return 0;
    }
    if (gWindows[Idx].Width == 0 || gWindows[Idx].Height == 0) {
        return 0;
    }
    Pages = BackupPageCount(gWindows[Idx].Width, gWindows[Idx].Height);
    if (Pages == 0) {
        return 0;
    }
    if (gWinBackup[Idx] != 0 && gWinBackupPages[Idx] == Pages) {
        return 1;
    }
    if (gWinBackup[Idx] != 0) {
        PhysicalMemoryFreePages(gWinBackup[Idx], gWinBackupPages[Idx]);
        gWinBackup[Idx] = 0;
        gWinBackupPages[Idx] = 0;
    }
    /* 换新页后内容未定义，必须清 Valid，禁止未填充就 WriteRect */
    gWinBackupValid[Idx] = 0;
    gWinBackup[Idx] = (UINT32 *)PhysicalMemoryAllocatePages(Pages);
    if (gWinBackup[Idx] == 0) {
        return 0;
    }
    gWinBackupPages[Idx] = Pages;
    return 1;
}


void PreallocWindowBackups(void) {
    int i;

    for (i = 0; i < MAX_WINS; i++) {
        if (gWindows[i].Active) {
            EnsureWindowBackupBuf(i);
        }
    }
}


/*
 * ForceFull：主题自下而上刚画完本窗、上层尚未覆盖时，必须整窗 ReadRect，
 * 否则 Occluded 路径会跳过重叠区，备份镂空，透视桌面/抬窗花屏。
 */
void BackupWindowAtEx(int Idx, int ForceFull) {
    const GUI_WINDOW *Win = &gWindows[Idx];
    UINT32 Rw;
    UINT32 Rh;
    UINT32 Bw;
    int HadValid;
    UINT32 *OldBuf;
    UINT32 Row;
    UINT32 Col;
    int WasVisible;

    if (Idx < 0 || Idx >= MAX_WINS || !Win->Active) {
        return;
    }
    if (Win->Width == 0 || Win->Height == 0) {
        return;
    }
    Rw = Win->Width;
    Rh = Win->Height;
    if (Win->X + Rw > gScreenWidth) {
        Rw = gScreenWidth - Win->X;
    }
    if (Win->Y + Rh > gScreenHeight) {
        Rh = gScreenHeight - Win->Y;
    }
    if (Rw == 0 || Rh == 0) {
        return;
    }

    HadValid = gWinBackupValid[Idx];
    OldBuf = gWinBackup[Idx];
    if (!EnsureWindowBackupBuf(Idx)) {
        gWinBackupValid[Idx] = 0;
        return;
    }
    if (gWinBackup[Idx] != OldBuf) {
        HadValid = 0;
    }

    WasVisible = gCursorVisible;
    ComposeBegin();
    GfxIrqEnter();
    CursorRestore();
    GfxIrqLeave();

    /*
     * ForceFull：compose 自下而上、上层尚未画时整窗 ReadRect（含将被盖住的区域）。
     * 非 ForceFull 且被挡（含上层阴影扫过）：只更新可见像素，被挡处保留旧备份。
     */
    if (!ForceFull && WindowOccludedByOtherOrShadow(Idx)) {
        UINT32 OldBw = gWinBackupW[Idx];
        UINT32 OldBh = gWinBackupH[Idx];

        if (!HadValid) {
            gWinBackupValid[Idx] = 0;
            if (WasVisible) {
                GfxIrqEnter();
                CursorPaint();
                GfxIrqLeave();
            }
            ComposeEnd();
            return;
        }
        Bw = Rw;
        for (Row = 0; Row < Rh; Row++) {
            for (Col = 0; Col < Rw; Col++) {
                UINT32 Px = Win->X + Col;
                UINT32 Py = Win->Y + Row;
                UINT32 *Dst = &gWinBackup[Idx][Row * Bw + Col];

                if (PixelCoveredByHigherWindow(Idx, Px, Py)) {
                    if (OldBuf != gWinBackup[Idx] && Col < OldBw && Row < OldBh) {
                        *Dst = OldBuf[Row * OldBw + Col];
                    }
                    continue;
                }
                *Dst = HalVideoReadPixel(Px, Py);
            }
        }
        gWinBackupW[Idx] = Rw;
        gWinBackupH[Idx] = Rh;
        gWinBackupValid[Idx] = 1;
        if (WasVisible) {
            GfxIrqEnter();
            CursorPaint();
            GfxIrqLeave();
        }
        ComposeEnd();
        return;
    }

    HalVideoReadRect(Win->X, Win->Y, Rw, Rh, gWinBackup[Idx]);
    gWinBackupW[Idx] = Rw;
    gWinBackupH[Idx] = Rh;
    gWinBackupValid[Idx] = 1;
    if (WasVisible) {
        GfxIrqEnter();
        CursorPaint();
        GfxIrqLeave();
    }
    ComposeEnd();
}


void BackupWindowAt(int Idx) {
    BackupWindowAtEx(Idx, 0);
}


void PaintWindowFromBackup(int Idx) {
    const GUI_WINDOW *Win = &gWindows[Idx];

    if (!Win->Active) {
        return;
    }
    if (gWinBackupValid[Idx] && gWinBackup[Idx] != 0) {
        HalVideoWriteRect(Win->X, Win->Y, gWinBackupW[Idx], gWinBackupH[Idx],
                       gWinBackup[Idx]);
        DrawWindowChromeAt(Idx);
        /* 阴影由 SyncWindowVisualsEx 第二遍统一画，避免先画影再被下层 WriteRect 打乱 */
        return;
    }
    DrawWindowAt(Idx);
}


void ShiftWinBackupsUp(int From, int To) {
    UINT32 *Buf = gWinBackup[From];
    UINT32 Bw = gWinBackupW[From];
    UINT32 Bh = gWinBackupH[From];
    UINT32 Bp = gWinBackupPages[From];
    int Bv = gWinBackupValid[From];
    int J;

    for (J = From; J < To; J++) {
        gWinBackup[J] = gWinBackup[J + 1];
        gWinBackupW[J] = gWinBackupW[J + 1];
        gWinBackupH[J] = gWinBackupH[J + 1];
        gWinBackupPages[J] = gWinBackupPages[J + 1];
        gWinBackupValid[J] = gWinBackupValid[J + 1];
    }
    gWinBackup[To] = Buf;
    gWinBackupW[To] = Bw;
    gWinBackupH[To] = Bh;
    gWinBackupPages[To] = Bp;
    gWinBackupValid[To] = Bv;
}


int RectIntersects(UINT32 Ax, UINT32 Ay, UINT32 Aw, UINT32 Ah,
                          UINT32 Bx, UINT32 By, UINT32 Bw, UINT32 Bh) {
    if (Aw == 0 || Ah == 0 || Bw == 0 || Bh == 0) {
        return 0;
    }
    return Ax < Bx + Bw && Bx < Ax + Aw && Ay < By + Bh && By < Ay + Ah;
}


void ClipRectToScreen(UINT32 *X, UINT32 *Y, UINT32 *W, UINT32 *H) {
    if (*W == 0 || *H == 0 || gScreenWidth == 0 || gScreenHeight == 0) {
        *W = 0;
        return;
    }
    if (*X >= gScreenWidth || *Y >= gScreenHeight) {
        *W = 0;
        *H = 0;
        return;
    }
    if (*X + *W > gScreenWidth) {
        *W = gScreenWidth - *X;
    }
    if (*Y + *H > gScreenHeight) {
        *H = gScreenHeight - *Y;
    }
}

void GuiBackupFocusWindow(void) {
    if (gFocusWin >= 0 && gFocusWin < MAX_WINS && gWindows[gFocusWin].Active) {
        BackupWindowAt(gFocusWin);
    }
}

