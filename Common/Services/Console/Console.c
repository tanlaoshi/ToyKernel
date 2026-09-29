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
/* -2=无 Job；-1=串口壳；>=0=发起 Shell 窗下标 */
static int gJobPromptWin = -2;
/* Worker 在 RunLine 内已 Release 并打过 toyos> 时，阻止 PromptAfterCommand 再打一次 */
static int gJobSkipAfterCommand;
/* -2=非命令输出；-1=串口；>=0=本条命令输出归属窗（跟发起窗，不跟焦点） */
static int gCmdOutWin = -2;

void Prompt(void) {
    gLen = 0;
    /* 仅当不在行首时换行：开窗欢迎语后已有 \\n，再打会空一行像「提示语不对」 */
    if (!gAtLineStart) {
        ConsoleWrite("\n");
    }
    HalConsoleWriteSerial("toyos> ");
    gAtLineStart = 0;
    if (HalConsoleVideoReady() && GuiFocusKind() == GUI_WIN_SHELL) {
        ConsoleSbBindFocus();
        ConsoleSbEnsureLive();
        ConsoleSbFeed("toyos> ");
        ConsoleDrawString("toyos> ", ThemeShellPrompt());
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

void ConsoleJobHoldPrompt(void) {
    if (HalConsoleOnly()) {
        gJobPromptWin = -1;
    } else if (GuiFocusKind() == GUI_WIN_SHELL) {
        gJobPromptWin = GuiFocusIndex();
    } else {
        gJobPromptWin = -1;
    }
    gJobSkipAfterCommand = 0;
    ConsoleWaitPrompt();
}

int ConsoleJobConsumeSkipAfterCommand(void) {
    if (!gJobSkipAfterCommand) {
        return 0;
    }
    gJobSkipAfterCommand = 0;
    return 1;
}

int ConsoleJobPromptPending(void) {
    return (gJobPromptWin != -2) ? 1 : 0;
}

void ConsoleJobShiftRaise(int Idx, int Top) {
    if (gJobPromptWin == Idx) {
        gJobPromptWin = Top;
    } else if (gJobPromptWin > Idx && gJobPromptWin <= Top) {
        gJobPromptWin--;
    }
    if (gCmdOutWin == Idx) {
        gCmdOutWin = Top;
    } else if (gCmdOutWin > Idx && gCmdOutWin <= Top) {
        gCmdOutWin--;
    }
}

void ConsoleCmdOutBegin(void) {
    if (HalConsoleOnly()) {
        gCmdOutWin = -1;
    } else if (GuiFocusKind() == GUI_WIN_SHELL) {
        gCmdOutWin = GuiFocusIndex();
    } else {
        gCmdOutWin = -1;
    }
}

void ConsoleCmdOutEnd(void) {
    gCmdOutWin = -2;
}

int ConsoleCmdOutOwner(void) {
    return gCmdOutWin;
}

void ConsoleWriteToJobShell(const char *Text) {
    int Owner = gJobPromptWin;
    int Cur;

    if (!Text) {
        return;
    }
    HalConsoleWriteSerial(Text);
    if (Owner < 0 || HalConsoleOnly()) {
        if (HalConsoleVideoReady() && GuiFocusKind() == GUI_WIN_SHELL) {
            ConsoleSbEnsureLive();
            ConsoleSbFeed(Text);
            ConsoleDrawString(Text, ThemeShellText());
        }
        return;
    }
    Cur = GuiFocusIndex();
    if (Cur == Owner && GuiShellWindowActive(Owner)) {
        if (HalConsoleVideoReady()) {
            ConsoleSbBindFocus();
            ConsoleSbEnsureLive();
            ConsoleSbFeed(Text);
            if (GuiShellAcceptsInput()) {
                ConsoleDrawString(Text, ThemeShellText());
            }
        } else {
            ConsoleSbWinFeed(Owner, Text);
        }
        return;
    }
    ConsoleSbWinFeed(Owner, Text);
}

void ConsoleJobReleasePrompt(void) {
    int Owner = gJobPromptWin;
    int Cur;

    if (Owner == -2) {
        return;
    }
    gJobPromptWin = -2;
    if (HalConsoleOnly() || Owner < 0) {
        ConsoleShowPrompt();
        gJobSkipAfterCommand = 1;
        return;
    }
    if (!GuiShellWindowActive(Owner)) {
        if (gWaitPrompt > 0) {
            gWaitPrompt--;
        }
        gJobSkipAfterCommand = 1;
        return;
    }
    Cur = GuiFocusIndex();
    if (Cur == Owner) {
        ConsoleShowPrompt();
        GuiConsolePush(gLine, gLen, gWaitPrompt);
        gJobSkipAfterCommand = 1;
        return;
    }
    /* 已切走：完成行在 WinFeed；槽内补提示符并清该窗 WaitPrompt */
    ConsoleSbWinFeed(Owner, "toyos> ");
    GuiConsoleSetWaitPrompt(Owner, 0);
    if (gWaitPrompt > 0) {
        gWaitPrompt--;
    }
    gJobSkipAfterCommand = 1;
}

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
    /* 串口始终可敲：开机即给提示符（勿等 GUI Shell；SNAKE 占焦点也在本终端输入） */
    HalConsoleWriteSerial("hint: type commands in THIS terminal (not QEMU window)\n");
    Prompt();
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
