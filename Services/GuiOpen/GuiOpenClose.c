/*
 * GuiOpenClose.c — 关窗与 Closing 等待（PR-S3-guiopen-1）
 *
 * 从 GuiOpen.c 原样搬家；不改语义。
 */
#include "GuiPrivate.h"
#include "UI.h"
#include "HalVideo.h"
#include "Hal.h"
#include "Debug.h"
#include "Theme.h"
#include "PhysicalMemory.h"
#include "SettingsUi.h"
#include "FilesUi.h"
#include "StoreUi.h"
#include "DevicesUi.h"
#include "EditUi.h"
#include "TtyUi.h"
#include "Desktop.h"
#include "DesktopPrivate.h"
#include "Console.h"

void CloseWindow(int Idx) {
    UINT32 X;
    UINT32 Y;
    UINT32 Ww;
    UINT32 Wh;
    int i;
    int SavedFocus;
    int WasUser;

    if (Idx < 0 || Idx >= MAX_WINS || !gWindows[Idx].Active) {
        return;
    }
    if (gWindows[Idx].Closing) {
        return;
    }
    gWindows[Idx].Closing = 1;
    WasUser = (gWindows[Idx].Kind == GUI_WIN_USER);
    if (gWindows[Idx].Kind == GUI_WIN_SHELL) {
        ConsoleSbWinForget(Idx);
    }
    if (gWindows[Idx].Kind == GUI_WIN_TTY) {
        TtyUiClose();
    }
    X = gWindows[Idx].X;
    Y = gWindows[Idx].Y;
    Ww = gWindows[Idx].Width;
    Wh = gWindows[Idx].Height;

    /*
     * USER 关窗不做淡出（真机淡出易与 exit 竞态 → 二次 exec #PF）。
     * 其它窗仍可淡出；淡出 under 只铺本窗矩形（GuiFade）。
     */
    if (!WasUser && ThemeWindowFadeSteps() != 0) {
        if (!gWinBackupValid[Idx]) {
            BackupWindowAt(Idx);
        }
        GuiAnimateWindowFade(Idx, 0);
    }

    if (WasUser) {
        gWindows[Idx].ClosePending = 1;
    }
    gWindows[Idx].Active = 0;
    gWindows[Idx].FixedSize = 0;
    gWindows[Idx].Kind = GUI_WIN_NONE;
    gWindows[Idx].TermSet = 0;
    gWindows[Idx].InputLen = 0;
    gWindows[Idx].WaitPrompt = 0;
    gWindows[Idx].PromptShown = 0;
    gWindows[Idx].InputLine[0] = 0;
    gWindows[Idx].UserButtonClick = -1;
    gWindows[Idx].UserClientClick = 0;
    gWindows[Idx].UserKeyCount = 0;
    {
        int Bi;
        for (Bi = 0; Bi < 4; Bi++) {
            gWindows[Idx].UserButtonUsed[Bi] = 0;
            gWindows[Idx].UserButtonLabel[Bi][0] = 0;
        }
    }
    gWinBackupValid[Idx] = 0;
    if (gWinBackup[Idx] != 0) {
        PhysicalMemoryFreePages(gWinBackup[Idx], gWinBackupPages[Idx]);
        gWinBackup[Idx] = 0;
        gWinBackupPages[Idx] = 0;
        gWinBackupW[Idx] = 0;
        gWinBackupH[Idx] = 0;
    }
    if (gDragWin == Idx) {
        gDragWin = -1;
    }
    if (gResizeWin == Idx) {
        gResizeWin = -1;
    }
    if (gHoverWin == Idx) {
        gHoverWin = -1;
    }
    if (gFocusWin == Idx) {
        gFocusWin = -1;
        for (i = MAX_WINS - 1; i >= 0; i--) {
            if (gWindows[i].Active) {
                gFocusWin = i;
                break;
            }
        }
    }

    SavedFocus = gFocusWin;

    if (WasUser) {
        UINT32 BarY;
        UINT32 Sw;
        UINT32 Sh;

        /*
         * USER 轻量关窗：只擦本窗 + 相交窗重画 + 任务栏置顶。
         * 禁止在此走 4K 全屏 Compose（exit 等待会超时 → Destroy 抢页 → #PF）。
         * 完整合成在 SchedulerExitUser Destroy 之后。
         */
        ComposeBeginEraseCursor();
        HalVideoClearClip();
        DesktopFillRect(X, Y, Ww, Wh);
        DesktopDrawRect(X, Y, Ww, Wh);
        for (i = 0; i < MAX_WINS; i++) {
            if (!gWindows[i].Active) {
                continue;
            }
            if (!RectIntersects(gWindows[i].X, gWindows[i].Y, gWindows[i].Width,
                                gWindows[i].Height, X, Y, Ww, Wh)) {
                continue;
            }
            gWinBackupValid[i] = 0;
            DrawWindowAtEx(i, 0);
            if (gWindows[i].Kind == GUI_WIN_SHELL) {
                GuiConsoleOpsPaintShellWindow(i);
            } else if (gWindows[i].Kind == GUI_WIN_SETTINGS) {
                gFocusWin = i;
                SettingsUiRepaint();
                gFocusWin = SavedFocus;
            } else if (gWindows[i].Kind == GUI_WIN_STORE) {
                gFocusWin = i;
                StoreUiRepaint();
                gFocusWin = SavedFocus;
            } else if (gWindows[i].Kind == GUI_WIN_DEVICES) {
                gFocusWin = i;
                DevicesUiRepaint();
                gFocusWin = SavedFocus;
            } else if (gWindows[i].Kind == GUI_WIN_FILES) {
                gFocusWin = i;
                FilesUiRepaint();
                gFocusWin = SavedFocus;
            } else if (gWindows[i].Kind == GUI_WIN_EDIT) {
                gFocusWin = i;
                EditUiRepaint();
                gFocusWin = SavedFocus;
            } else if (gWindows[i].Kind == GUI_WIN_TTY) {
                gFocusWin = i;
                TtyUiRepaint();
                gFocusWin = SavedFocus;
            } else if (gWindows[i].Kind == GUI_WIN_USER) {
                PaintUserClient(i);
            }
            BackupWindowAtEx(i, 1);
        }
        TaskbarGeom(&BarY, &Sw, &Sh);
        /*
         * Shell ConsoleWrite 常留下客户区 clip。DesktopFillRect 走 WriteRect
         *（无视 clip）先擦掉整条栏，而 DrawTaskbarRaw 的 Fill/Alpha 受 clip
         * → 栏没了（与 DesktopTickClock 同坑，见 Desktop.c）。
         */
        HalVideoClearClip();
        DesktopFillRect(0, BarY, Sw, TASKBAR_H);
        DrawTaskbarRaw();
        gFocusWin = SavedFocus;
        GfxIrqEnter();
        CursorPaint();
        GfxIrqLeave();
        HalVideoPresentFlush();
        ComposeEnd();
    } else {
        for (i = 0; i < MAX_WINS; i++) {
            if (gWindows[i].Active) {
                gWinBackupValid[i] = 0;
            }
        }
        GuiComposeThemeScene();
        gFocusWin = SavedFocus;
    }

    GuiFocusApply();
    gWindows[Idx].Closing = 0;
    DebugWrite("Gui: closed (clip)\n");
}

void GuiCloseAllUserWindows(void) {
    int i;

    for (i = MAX_WINS - 1; i >= 0; i--) {
        if (gWindows[i].Closing) {
            continue;
        }
        if (gWindows[i].Active && gWindows[i].Kind == GUI_WIN_USER) {
            CloseWindow(i);
        }
    }
}

/*
 * 真机 SMP：点 × 在 Gui 核上关窗时，用户核可能已 exit。
 * 必须等到 Closing 清零再 Destroy（禁止自旋上限超时后强拆页表）。
 */
void GuiWaitNoWindowClosing(void) {
    UINTN Spin = 0;

    for (;;) {
        int i;
        int Busy = 0;

        for (i = 0; i < MAX_WINS; i++) {
            if (gWindows[i].Closing) {
                Busy = 1;
                break;
            }
        }
        if (!Busy) {
            return;
        }
        if (Spin == 0) {
            DebugWrite("Gui: wait closing (smp)\n");
        }
        Spin++;
        if ((Spin & 0xFFFFFu) == 0) {
            DebugWrite("Gui: still closing...\n");
        }
        HalCpuRelax();
    }
}
