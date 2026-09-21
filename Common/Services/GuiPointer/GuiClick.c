/*
 * GuiClick.c — 按下命中：关窗、菜单、焦点与拖放武装
 * 核心：GuiPointer.c
 */
#include "GuiPrivate.h"
#include "HalVideo.h"
#include "Hal.h"
#include "Debug.h"
#include "Desktop.h"
#include "SettingsUi.h"
#include "FilesUi.h"
#include "StoreUi.h"
#include "DevicesUi.h"
#include "EditUi.h"
#include "Console.h"
#include "Process.h"
#include "ToySerialLog.h"

int GuiHandleClick(UINT32 X, UINT32 Y) {
    int i;
    int Hit;
    DESKTOP_ACTION Act = DESKTOP_ACTION_NONE;
    char ExecPath[96];

    /* 关闭钮可能被其它窗口挡住；先扫一遍所有窗口的 × 区域 */
    for (i = MAX_WINS - 1; i >= 0; i--) {
        if (gWindows[i].Active && PointInClose(&gWindows[i], X, Y)) {
            CloseWindow(i);
            return 1;
        }
    }

    ExecPath[0] = 0;
    /*
     * 开始菜单 / 网络托盘聚焦优先级：
     * 1) 点在菜单/flyout/开始钮/托盘 → DesktopHandleClick
     * 2) 点在菜单外且落在窗上 → HandleTaskbarClick 已收起，再 fall through 聚焦置顶
     * 3) 菜单未开时任务栏仍优先于窗（开始钮）
     */
    {
        int DoDesktop = 0;

        if (DesktopStartMenuIsOpen() || DesktopNetTrayIsOpen()) {
            if (DesktopHandleClick(X, Y, &Act, ExecPath, sizeof(ExecPath))) {
                DoDesktop = 1;
            }
            /* 未命中：已收起；继续下面 Raise 窗 */
        } else if (DesktopClickOnTaskbar(X, Y) &&
                   DesktopHandleClick(X, Y, &Act, ExecPath, sizeof(ExecPath))) {
            DoDesktop = 1;
        }
        if (DoDesktop) {
            if (DesktopIconDragActive()) {
                GfxIrqEnter();
                CursorRestore();
                GfxIrqLeave();
            }
            if (Act == DESKTOP_ACTION_SHELL) {
                (void)GuiOpenShell();
            } else if (Act == DESKTOP_ACTION_SETTINGS) {
                (void)GuiOpenSettings();
            } else if (Act == DESKTOP_ACTION_FILES) {
                (void)GuiOpenFiles();
            } else if (Act == DESKTOP_ACTION_STORE) {
                (void)GuiOpenStore();
            } else if (Act == DESKTOP_ACTION_DEVICES) {
                (void)GuiOpenDevices();
            } else if (Act == DESKTOP_ACTION_EXEC) {
                if (ExecPath[0]) {
                    DebugWrite("desktop: exec ");
                    DebugWrite(ExecPath);
                    DebugWrite("\n");
                    (void)ProcessExec(ExecPath);
                }
            } else if (Act == DESKTOP_ACTION_SHUTDOWN) {
                HalCpuShutdown();
            } else if (Act == DESKTOP_ACTION_REBOOT) {
                HalCpuReboot();
            }
            return 1;
        }
    }

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
            } else {
                EditUiOnClick(X, Y);
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
        return 1;
    }
    /* 未点中窗口：桌面图标（双击打开） / 开始菜单 */
    {
        DESKTOP_ACTION Act = DESKTOP_ACTION_NONE;
        char ExecPath[96];

        ExecPath[0] = 0;
        if (!DesktopHandleClick(X, Y, &Act, ExecPath, sizeof(ExecPath))) {
            return 0;
        }
        /* PR-G-desk-1：与窗标题拖一致，武装拖放前先擦光标，避免留下光标脏块 */
        if (DesktopIconDragActive()) {
            GfxIrqEnter();
            CursorRestore();
            GfxIrqLeave();
        }
        if (Act == DESKTOP_ACTION_SHELL) {
            (void)GuiOpenShell();
        } else if (Act == DESKTOP_ACTION_SETTINGS) {
            (void)GuiOpenSettings();
        } else if (Act == DESKTOP_ACTION_FILES) {
            (void)GuiOpenFiles();
        } else if (Act == DESKTOP_ACTION_STORE) {
            (void)GuiOpenStore();
        } else if (Act == DESKTOP_ACTION_DEVICES) {
            (void)GuiOpenDevices();
        } else if (Act == DESKTOP_ACTION_EXEC) {
            /* PR-G-desk-2：与 Files 双击 ELF 同路径；阻塞至进程退出 */
            if (ExecPath[0]) {
                DebugWrite("desktop: exec ");
                DebugWrite(ExecPath);
                DebugWrite("\n");
                (void)ProcessExec(ExecPath);
            }
        } else if (Act == DESKTOP_ACTION_SHUTDOWN) {
            HalCpuShutdown();
        } else if (Act == DESKTOP_ACTION_REBOOT) {
            HalCpuReboot();
        }
        return 1;
    }
}
