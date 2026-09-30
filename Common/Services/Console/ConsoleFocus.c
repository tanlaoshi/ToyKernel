/*
 * ConsoleFocus.c — Shell 焦点 / 开窗 / 重画（PR-S3-console-2）
 */
#include "Console.h"
#include "ConsolePrivate.h"
#include "UI.h"
#include "Hal.h"
#include "Gui.h"
#include "Locale.h"
#include "SettingsUi.h"
#include "LibWrite.h"

/* 将控制台输出限制在当前焦点窗口客户区内（不重置光标） */
void ConsoleFocusSave(void) {
    int Idx = GuiFocusIndex();

    if (Idx >= 0 && GuiShellWindowActive(Idx)) {
        ConsoleSbWinSave(Idx);
    }
    GuiConsolePush(gLine, gLen, gWaitPrompt);
}

void ConsoleFocusLoad(void) {
    int Idx = GuiFocusIndex();

    GuiConsolePull(gLine, &gLen, &gWaitPrompt);
    if (Idx >= 0 && GuiShellWindowActive(Idx)) {
        ConsoleSbWinLoad(Idx);
    }
    /* 非 Shell 焦点（如 Settings）不碰控制台绘制 */
    if (!GuiShellAcceptsInput()) {
        return;
    }
    if (GuiConsoleHasDisplay()) {
        GuiFocusApplyClip();
        if (GuiConsoleNeedsPrompt()) {
            /*
             * 改字体/ThemeApply 会清 PromptShown；若 scrollback 仍在，
             * 勿再打欢迎语（否则接在已有 toyos> 后面）。
             */
            if (!ConsoleSbHasContent()) {
                ConsoleWrite(LocStr(MSG_CON_WELCOME));
                ConsoleWrite("\n");
                Prompt();
            } else {
                ConsoleSbRepaint();
            }
            GuiConsoleMarkPrompt();
            GuiFocusSave();
        } else if (ConsoleSbHasContent()) {
            ConsoleSbRepaint();
        }
        return;
    }
    /*
     * 无客户区光标：主题清空后的 Shell，或尚未 OnShellOpened 的新窗。
     * 新窗 OpenShell 先 PromptShown=1 抑制此处；OnShellOpened 再画。
     */
    if (GuiConsoleNeedsPrompt()) {
        if (!ConsoleSbHasContent()) {
            GuiFocusHome();
            ConsoleWrite(LocStr(MSG_CON_WELCOME));
            ConsoleWrite("\n");
            Prompt();
        }
        GuiConsoleMarkPrompt();
        GuiFocusSave();
    }
}

void ConsoleBindFocus(void) {
    GuiFocusApply();
}

/* 初始化：无 Shell 时仅串口就绪文案；开窗 / ConsoleSerialRun 再画欢迎语与 toyos> */
void ConsoleInitialize(void) {
    GUI_CONSOLE_OPS Ops;

    Ops.FocusSave = ConsoleFocusSave;
    Ops.FocusLoad = ConsoleFocusLoad;
    Ops.OnShellOpened = ConsoleOnShellOpened;
    Ops.PaintShellWindow = ConsolePaintShellWindow;
    GuiRegisterConsoleOps(&Ops);
    LibWriteRegister(ConsoleWrite);

    gLen = 0;
    gWaitPrompt = 0;
    gAtLineStart = 1;
    HalConsoleWriteSerial(LocStr(MSG_CON_READY));
    HalConsoleWriteSerial("\n");
    /*
     * 勿在此 Prompt：SchedulerStart 后 AP idle 会抢行。
     * 桌面路径由 ShellTask 首轮打串口 toyos>；ConsoleOnly 由 ConsoleSerialRun。
     * 串口收键不依赖 GUI 开窗（见 ConsoleEnsureShell FromSerial）。
     */
    HalConsoleWriteSerial("hint: type commands in THIS terminal (not QEMU window)\n");
}

void ConsoleOnShellOpened(void) {
    int Idx;

    /*
     * 勿用 GuiShellAcceptsInput：刚 OpenChromeDefer 时 z-order 可能仍判遮挡，
     * 会整段跳过欢迎语。按窗是否为 Shell 即可。
     */
    Idx = GuiFocusIndex();
    if (!GuiShellWindowActive(Idx)) {
        return;
    }
    /* 新窗槽为空 → 欢迎语；已有槽（主题重画）→ 重绘本窗历史 */
    ConsolePaintShellWindow(Idx);
}

/* PR-G8：主题合成时按窗下标画 Shell，不要求当前可输入/未遮挡 */
void ConsolePaintShellWindow(int Idx) {
    int Saved;

    if (!GuiShellWindowActive(Idx)) {
        return;
    }
    Saved = GuiFocusIndex();
    if (Saved >= 0 && Saved != Idx && GuiShellWindowActive(Saved)) {
        ConsoleSbWinSave(Saved);
    }
    GuiSetFocusWindow(Idx);
    ConsoleSbWinLoad(Idx);
    /*
     * 开开始菜单等会走 GuiComposeThemeScene → 本函数。
     * 若仍 SbReset+欢迎语，ps/help 输出会被清掉；有行缓冲则重绘恢复。
     */
    if (ConsoleSbWinHasContent(Idx) || ConsoleSbHasContent()) {
        ConsoleSbRepaint();
        /* Theme 清过 PromptShown；重绘后勿让 FocusLoad 再打欢迎语 */
        GuiConsoleMarkPrompt();
        ConsoleSbWinSave(Idx);
        /* 与空窗路径一致：勿把客户区 clip 留给随后的开始菜单/任务栏 */
        HalVideoClearClip();
        if (Saved >= 0 && GuiWindowKind(Saved) != GUI_WIN_NONE) {
            GuiSetFocusWindow(Saved);
            if (GuiShellWindowActive(Saved)) {
                ConsoleSbWinLoad(Saved);
            }
        }
        return;
    }
    gLen = 0;
    gWaitPrompt = 0;
    gAtLineStart = 1;
    ConsoleSbReset();
    GuiFocusClearClient();
    GuiFocusHome();
    ConsoleWrite(LocStr(MSG_CON_WELCOME));
    ConsoleWrite("\n");
    Prompt();
    GuiConsoleMarkPrompt();
    ConsoleSbWinSave(Idx);
    GuiConsolePush(gLine, gLen, gWaitPrompt);
    HalVideoClearClip();
    GuiBackupFocusWindow();
    if (Saved >= 0 && GuiWindowKind(Saved) != GUI_WIN_NONE) {
        GuiSetFocusWindow(Saved);
        if (GuiShellWindowActive(Saved)) {
            ConsoleSbWinLoad(Saved);
        }
    }
}

void ConsoleRepaintShellWindows(void) {
    int Saved;
    int i;
    GUI_WIN_KIND SavedKind;

    Saved = GuiFocusIndex();
    SavedKind = GuiFocusKind();

    for (i = 0; i < GUI_MAX_WINS; i++) {
        if (GuiWindowKind(i) != GUI_WIN_SHELL) {
            continue;
        }
        /* 置顶后再画，避免欢迎语/prompt 写穿上层 Settings */
        GuiRaiseToFront(i);
        ConsoleOnShellOpened();
    }

    if (Saved >= 0 && GuiWindowKind(Saved) != GUI_WIN_NONE) {
        GuiRaiseToFront(Saved);
        if (SavedKind == GUI_WIN_SHELL) {
            ConsoleFocusLoad();
        } else if (SavedKind == GUI_WIN_SETTINGS) {
            SettingsUiRefresh();
        }
    }
    HalVideoClearClip();
}
