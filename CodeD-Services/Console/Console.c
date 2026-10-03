/*
 * Console.c — Shell 提示符与 Job 输出（PR-S3-console-2）
 *
 * 焦点 / 开窗重画见 ConsoleFocus.c。
 */
#include "Console.h"
#include "ConsolePrivate.h"
#include "UI.h"
#include "Hal.h"
#include "Gui.h"
#include "Font.h"
#include "Scheduler.h"
#include "Locale.h"

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
static int gBootReadyAnnounced;

/* 用户 ELF read(0)：Shell 泄串口/键盘入环；须 ≥ 真机单轮 MaxRx，防粘贴丢字 */
#define STDIN_Q_CAP 256
static char gStdinQ[STDIN_Q_CAP];
static int gStdinR;
static int gStdinW;
static int gStdinN;

int ConsoleStdinUserHold(void) {
    return gWaitPrompt > 0 || SchedulerLiveUserApps();
}

void ConsoleStdinFlush(void) {
    gStdinR = 0;
    gStdinW = 0;
    gStdinN = 0;
}

void ConsoleStdinPut(char C) {
    if (gStdinN >= STDIN_Q_CAP) {
        return;
    }
    gStdinQ[gStdinW] = C;
    gStdinW++;
    if (gStdinW >= STDIN_Q_CAP) {
        gStdinW = 0;
    }
    gStdinN++;
}

char ConsoleStdinGetChar(void) {
    for (;;) {
        if (gStdinN > 0) {
            char C = gStdinQ[gStdinR];
            gStdinR++;
            if (gStdinR >= STDIN_Q_CAP) {
                gStdinR = 0;
            }
            gStdinN--;
            return C;
        }
        if (HalSerialDataReady()) {
            return HalSerialReadChar();
        }
        HalCpuHalt();
    }
}

void ConsoleAnnounceBootReady(void) {
    if (gBootReadyAnnounced) {
        return;
    }
    gBootReadyAnnounced = 1;
    HalConsoleWriteSerial(LocStr(MSG_CON_READY));
    HalConsoleWriteSerial("\n");
    HalConsoleWriteSerial("hint: type commands in THIS terminal (not QEMU window)\n");
    Prompt();
}

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
        ConsoleStdinFlush();
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
