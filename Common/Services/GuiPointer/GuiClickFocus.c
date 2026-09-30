/*
 * GuiClickFocus.c — USER 命中与置顶后按窗种类分发（PR-F-guiclick-1）
 *
 * 从 GuiClick.c 抽出；不改点击语义。
 */
#include "GuiPrivate.h"
#include "HalVideo.h"
#include "Debug.h"
#include "SettingsUi.h"
#include "FilesUi.h"
#include "StoreUi.h"
#include "DevicesUi.h"
#include "EditUi.h"
#include "TtyUi.h"
#include "Console.h"

int GuiClickTryUserClient(UINT32 X, UINT32 Y) {
    int i;
    int Hit;

    /*
     * USER 按钮：在 Raise/Sync 之前命中顶层窗。
     * SyncWindowVisuals 会重贴备份；Raise 会搬槽，导致之后命中失败或事件写到错槽。
     */
    for (i = MAX_WINS - 1; i >= 0; i--) {
        if (!PointInWindow(&gWindows[i], X, Y)) {
            continue;
        }
        if (gWindows[i].Kind == GUI_WIN_USER && !PointInTitle(&gWindows[i], X, Y)) {
            if (PointInResizeCorner(&gWindows[i], X, Y)) {
                break;
            }
            Hit = UserButtonHit(i, X, Y);
            if (Hit >= 0) {
                gWindows[i].UserButtonClick = Hit;
                GuiFocusSave();
                RaiseWindow(i);
                /* 轻量置顶：勿 Sync 整桌（避免闪烁/吞事件） */
                GuiFocusApply();
                return 1;
            }
            /* 客户区：记下点击并置顶重画。只 Raise 不合成时窗仍画在 Shell 下面。 */
            {
                UINT32 Cx = gWindows[i].X + 1 + GUI_CLIENT_PAD;
                UINT32 Cy = gWindows[i].Y + TITLE_HEIGHT + GUI_CLIENT_PAD;
                if (X >= Cx && Y >= Cy) {
                    gWindows[i].UserClientClick = 1;
                    gWindows[i].UserClickX = X - Cx;
                    gWindows[i].UserClickY = Y - Cy;
                    GuiFocusSave();
                    GuiRaiseToFront(i);
                    return 1;
                }
            }
        }
        break; /* 顶层命中窗不是按钮，走下方通用逻辑 */
    }
    return 0;
}

static void GuiClickDispatchKind(UINT32 X, UINT32 Y) {
    if (GuiFocusKind() == GUI_WIN_SETTINGS) {
        if (PointInTitle(&gWindows[gFocusWin], X, Y)) {
            SettingsUiRepaint();
            /* 客户区重绘后强制刷新标题，清光标/透视残块 */
            GuiFrameBufferBegin();
            DrawWindowChromeAt(gFocusWin);
            GuiFrameBufferEnd();
        }
        /* 客户区：按下高亮由 OnPointer；抬起才触发（见 SettingsUiOnPointer / StoreUiOnPointer） */
    } else if (GuiFocusKind() == GUI_WIN_STORE) {
        if (PointInTitle(&gWindows[gFocusWin], X, Y)) {
            StoreUiRepaint();
            GuiFrameBufferBegin();
            DrawWindowChromeAt(gFocusWin);
            GuiFrameBufferEnd();
        }
        /* 客户区：同 Settings，不在按下时 OnClick */
    } else if (GuiFocusKind() == GUI_WIN_DEVICES) {
        if (PointInTitle(&gWindows[gFocusWin], X, Y)) {
            DevicesUiRepaint();
            GuiFrameBufferBegin();
            DrawWindowChromeAt(gFocusWin);
            GuiFrameBufferEnd();
        } else {
            DevicesUiOnClick(X, Y);
        }
    } else if (GuiFocusKind() == GUI_WIN_FILES) {
        if (PointInTitle(&gWindows[gFocusWin], X, Y)) {
            FilesUiRepaint();
            GuiFrameBufferBegin();
            DrawWindowChromeAt(gFocusWin);
            GuiFrameBufferEnd();
        } else {
            FilesUiOnClick(X, Y);
        }
    } else if (GuiFocusKind() == GUI_WIN_EDIT) {
        if (PointInTitle(&gWindows[gFocusWin], X, Y)) {
            EditUiRepaint();
            GuiFrameBufferBegin();
            DrawWindowChromeAt(gFocusWin);
            GuiFrameBufferEnd();
        }
        /* 客户区：悬停/按下由 OnPointer；抬起触发 Save（PR-GUI-migrate-edit） */
    } else if (GuiFocusKind() == GUI_WIN_TTY) {
        if (PointInTitle(&gWindows[gFocusWin], X, Y)) {
            TtyUiRepaint();
            GuiFrameBufferBegin();
            DrawWindowChromeAt(gFocusWin);
            GuiFrameBufferEnd();
        } else {
            TtyUiOnClick(X, Y);
        }
    } else if (GuiFocusKind() == GUI_WIN_SHELL) {
        if (PointInTitle(&gWindows[gFocusWin], X, Y)) {
            if (!gWinBackupValid[gFocusWin]) {
                GuiConsoleOpsOnShellOpened();
            }
        } else {
            ConsoleOnClick(X, Y);
            if (!gWinBackupValid[gFocusWin]) {
                GuiConsoleOpsOnShellOpened();
            }
        }
    } else if (gFocusWin >= 0) {
        BackupWindowAt(gFocusWin);
    }
}

int GuiClickTryRaiseWindow(UINT32 X, UINT32 Y) {
    int i;

    for (i = MAX_WINS - 1; i >= 0; i--) {
        if (!PointInWindow(&gWindows[i], X, Y)) {
            continue;
        }
        GuiFocusSave();
        RaiseWindow(i);
        SyncWindowVisuals();
        GuiFocusApply();
        DebugWrite("Gui: focus ");
        DebugWrite(gWindows[gFocusWin].Title);
        DebugWrite("\n");

        if (PointInResizeCorner(&gWindows[gFocusWin], X, Y) &&
            !PointOnAnyClose(X, Y)) {
            GfxIrqEnter();
            CursorRestore();
            GfxIrqLeave();
            RaiseWindow(gFocusWin);
            GuiResizeBegin(gFocusWin, X, Y);
            /* 按下瞬间须立刻换成 E/S/SE 形，勿等下一次 Move */
            GuiCursorPaint();
            return 1;
        }
        if (PointInTitle(&gWindows[gFocusWin], X, Y) &&
            !PointOnAnyClose(X, Y)) {
            GfxIrqEnter();
            CursorRestore();
            GfxIrqLeave();
            RaiseWindow(gFocusWin);
            gResizeWin = -1;
            gDragWin = gFocusWin;
            gDragOffX = (INT32)X - (INT32)gWindows[gFocusWin].X;
            gDragOffY = (INT32)Y - (INT32)gWindows[gFocusWin].Y;
            gDragArmed = 1;
        }
        GuiClickDispatchKind(X, Y);
        return 1;
    }
    return 0;
}
