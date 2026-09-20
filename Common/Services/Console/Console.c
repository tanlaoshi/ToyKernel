/*
 * Console.c — Shell 焦点、提示符与重画（核心）
 * 辅助：ConsoleWrite.c / ConsoleInput.c / ConsoleScroll.c
 *
 * 从 Console.c 单体迁出；只搬家、不改逻辑。
 */
#include "Console.h"
#include "ConsolePrivate.h"
#include "UI.h"
#include "Hal.h"
#include "Gui.h"
#include "Font.h"
#include "SettingsUi.h"
#include "Locale.h"
#include "HIDKeyboard.h"
#include "LibWrite.h"
#include "ShellCommands.h"

char gLine[LINE_MAX];
int gLen;
int gWaitPrompt;
int gPromptSuspend;
int gAtLineStart = 1;

void Prompt(void) {
    gLen = 0;
    if (!gAtLineStart) {
        ConsoleWrite("\n");
    }
    HalConsoleWriteSerial("toyos> ");
    gAtLineStart = 0;
    if (HalConsoleVideoReady() && GuiFocusKind() == GUI_WIN_SHELL) {
        ConsoleSbEnsureLive();
        ConsoleSbFeed("toyos> ");
        ConsoleDrawString("toyos> ", ThemeShellPrompt());
    } else {
        ConsoleSbFeed("toyos> ");
    }
}

void ConsoleWaitPrompt(void) {
    gWaitPrompt++;
}

void ConsoleShowPrompt(void) {
    if (gWaitPrompt > 0) {
        gWaitPrompt--;
    }
    if (gWaitPrompt == 0) {
        Prompt();
    }
}

void ConsoleNotify(const char *Text) {
    if (!ConsolePromptSuspended()) {
        ConsoleWrite("\n");
    }
    if (Text != 0) {
        ConsoleWrite(Text);
    }
}

void ConsoleSuspendPrompt(void) {
    gPromptSuspend++;
}

void ConsoleResumePrompt(void) {
    if (gPromptSuspend > 0) {
        gPromptSuspend--;
    }
    if (gPromptSuspend == 0 && gWaitPrompt == 0) {
        Prompt();
    }
}

int ConsolePromptSuspended(void) {
    return gPromptSuspend > 0;
}


/* 将控制台输出限制在当前焦点窗口客户区内（不重置光标） */
void ConsoleFocusSave(void) {
    GuiConsolePush(gLine, gLen, gWaitPrompt);
}

void ConsoleFocusLoad(void) {
    GuiConsolePull(gLine, &gLen, &gWaitPrompt);
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
            }
            GuiConsoleMarkPrompt();
            GuiFocusSave();
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

/* 初始化：无 Shell 时仅串口提示；开窗后由 ConsoleOnShellOpened 画欢迎语 */
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
}

void ConsoleOnShellOpened(void) {
    if (!GuiShellAcceptsInput()) {
        return;
    }
    ConsolePaintShellWindow(GuiFocusIndex());
}

/* PR-G8：主题合成时按窗下标画 Shell，不要求当前可输入/未遮挡 */
void ConsolePaintShellWindow(int Idx) {
    int Saved;

    if (!GuiShellWindowActive(Idx)) {
        return;
    }
    Saved = GuiFocusIndex();
    GuiSetFocusWindow(Idx);
    /*
     * 开开始菜单等会走 GuiComposeThemeScene → 本函数。
     * 若仍 SbReset+欢迎语，ps/help 输出会被清掉；有行缓冲则重绘恢复。
     */
    if (ConsoleSbHasContent()) {
        ConsoleSbRepaint();
        /* Theme 清过 PromptShown；重绘后勿让 FocusLoad 再打欢迎语 */
        GuiConsoleMarkPrompt();
        if (Saved >= 0 && GuiWindowKind(Saved) != GUI_WIN_NONE) {
            GuiSetFocusWindow(Saved);
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
    GuiFocusSave();
    HalVideoClearClip();
    GuiBackupFocusWindow();
    if (Saved >= 0 && GuiWindowKind(Saved) != GUI_WIN_NONE) {
        GuiSetFocusWindow(Saved);
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
