/*
 * GuiClickDesktop.c — 桌面/菜单/托盘点击与 DESKTOP_ACTION（PR-F-guiclick-1）
 *
 * 从 GuiClick.c 抽出；不改点击语义。
 */
#include "GuiPrivate.h"
#include "Hal.h"
#include "Debug.h"
#include "Desktop.h"
#include "Console.h"
#include "Process.h"

static void GuiClickRunDesktopAction(DESKTOP_ACTION Act, const char *ExecPath) {
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
        if (ExecPath != NULL && ExecPath[0]) {
            DebugWrite("desktop: exec ");
            DebugWrite(ExecPath);
            DebugWrite("\n");
            (void)ProcessExec(ExecPath);
            /* 用户 ELF 往串口打字会冲掉 toyos>；补一行提示，宿主仍可敲 */
            ConsoleWrite("\n");
            ConsoleShowPrompt();
        }
    } else if (Act == DESKTOP_ACTION_SHUTDOWN) {
        HalCpuShutdown();
    } else if (Act == DESKTOP_ACTION_REBOOT) {
        HalCpuReboot();
    }
}

int GuiClickTryDesktopBar(UINT32 X, UINT32 Y) {
    DESKTOP_ACTION Act = DESKTOP_ACTION_NONE;
    char ExecPath[96];
    int DoDesktop = 0;

    ExecPath[0] = 0;
    /*
     * 开始菜单 / 网络托盘聚焦优先级：
     * 1) 点在菜单/flyout/开始钮/托盘 → DesktopHandleClick
     * 2) 点在菜单外且落在窗上 → HandleTaskbarClick 已收起，再 fall through 聚焦置顶
     * 3) 菜单未开时任务栏仍优先于窗（开始钮）
     */
    if (DesktopStartMenuIsOpen() || DesktopNetTrayIsOpen()) {
        if (DesktopHandleClick(X, Y, &Act, ExecPath, sizeof(ExecPath))) {
            DoDesktop = 1;
        }
        /* 未命中：已收起；继续下面 Raise 窗 */
    } else if (DesktopClickOnTaskbar(X, Y) &&
               DesktopHandleClick(X, Y, &Act, ExecPath, sizeof(ExecPath))) {
        DoDesktop = 1;
    }
    if (!DoDesktop) {
        return 0;
    }
    if (DesktopIconDragActive()) {
        GfxIrqEnter();
        CursorRestore();
        GfxIrqLeave();
    }
    GuiClickRunDesktopAction(Act, ExecPath);
    return 1;
}

int GuiClickTryDesktopMiss(UINT32 X, UINT32 Y) {
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
    GuiClickRunDesktopAction(Act, ExecPath);
    return 1;
}
