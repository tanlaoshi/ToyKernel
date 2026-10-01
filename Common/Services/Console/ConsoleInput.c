/*
 * ConsoleInput.c — 按键、回车（串口壳见 ConsoleSerial.c）
 */
#include "Console.h"
#include "ConsolePrivate.h"
#include "UI.h"
#include "Hal.h"
#include "Gui.h"
#include "GuiPrivate.h"
#include "Font.h"
#include "SettingsUi.h"
#include "Locale.h"
#include "LibWrite.h"

/* 空桌面开 Shell。返回 0 失败；1 可输入；2 刚打开（吞触发键）。 */
static int ConsoleEnsureShell(int FromSerial) {
    /* PR-B1：HalConsoleOnly — 串口子集不要求 GUI Shell 窗 */
    if (HalConsoleOnly()) {
        return 1;
    }
    if (GuiShellAcceptsInput()) {
        return 1;
    }
    if (FromSerial) {
        /*
         * 串口始终可敲：不抢焦点、不开窗（与文件头注释一致）。
         * 已有 Shell 则置顶便于 GUI 同步；开窗交给 shell 命令 / 桌面点击。
         * （曾在此 GuiOpenShell：淡入若卡住 → Enter 无 toyos>、像死机。）
         */
        if (FocusExistingKind(GUI_WIN_SHELL, 0, "shell-serial") >= 0) {
            return 1;
        }
        return 1;
    }
    if (GuiFocusKind() != GUI_WIN_NONE) {
        return 0;
    }
    if (GuiOpenShell() < 0) {
        HalConsoleWriteSerial("shell: no free window\n");
        return 0;
    }
    /* 欢迎语已在 GuiOpenShell 淡入前画好 */
    return 2;
}

/* 处理可打印字符输入；FromSerial=1 时只回显串口，不往非 Shell 窗上画 */
void ConsoleOnCharEx(char C, int FromSerial) {
    int Ensured;

    if (ConsoleStdinUserHold()) {
        /* 误入 shell 行缓冲：改送用户 stdin（串口路径本不应到此） */
        ConsoleStdinPut(C);
        return;
    }
    Ensured = ConsoleEnsureShell(FromSerial);
    if (Ensured == 0) {
        return;
    }
    if (Ensured == 2) {
        /* 开窗触发键（如 /）不进入输入行 */
        return;
    }
    if (C < 32 || C > 126) {
        return;
    }
    if (gLen >= LINE_MAX - 1) {
        return;
    }
    ConsoleHistOnEdit();
    if (!FromSerial || GuiShellAcceptsInput()) {
        ConsoleSbBindFocus();
        ConsoleSbEnsureLive();
    }
    gLine[gLen++] = C;
    if (!FromSerial || GuiShellAcceptsInput()) {
        ConsoleSbFeedChar(C);
    }
    HalConsolePutChar(C);
    if (HalConsoleVideoReady() && GuiShellAcceptsInput()) {
        ConsoleDrawChar(C, ThemeShellText());
    }
}

void ConsoleOnChar(char C) {
    ConsoleOnCharEx(C, 0);
}

/* 处理退格键 */
void ConsoleOnBackspaceEx(int FromSerial) {
    if (!HalConsoleOnly() && !GuiShellAcceptsInput() && !FromSerial) {
        return;
    }
    if (gLen <= 0) {
        return;
    }
    ConsoleHistOnEdit();
    if (!FromSerial || GuiShellAcceptsInput()) {
        ConsoleSbEnsureLive();
    }
    gLen--;
    if (!FromSerial || GuiShellAcceptsInput()) {
        ConsoleSbBackspace();
    }
    if (HalConsoleVideoReady() && GuiShellAcceptsInput()) {
        GuiFrameBufferBegin();
        GuiFocusApplyClip();
        HalConsoleEraseLastChar();
        {
            UINT32 X;
            UINT32 Y;

            HalConsoleGetTextCursor(&X, &Y);
            GuiBackupSyncRect(X, Y, FontAdvanceX(), FontCellH());
        }
        GuiFocusSyncCursor();
        GuiPresentShellEchoMark();
        GuiFrameBufferEnd();
    }
    HalConsoleBackspaceSerial();
}

void ConsoleOnBackspace(void) {
    ConsoleOnBackspaceEx(0);
}

void ConsoleDiscardInput(void) {
    gLen = 0;
}

void ConsoleCancelInput(void) {
    if (!HalConsoleOnly() && !GuiShellAcceptsInput()) {
        return;
    }
    while (gLen > 0) {
        ConsoleOnBackspace();
    }
    ConsoleWrite("^C\n");
    if (gWaitPrompt == 0 && !ConsolePromptSuspended()) {
        Prompt();
    }
}

/* 强制结束 listen 等对提示符的挂起（可叠多层） */
void ConsoleForceResumePrompt(void) {
    gPromptSuspend = 0;
    if (gWaitPrompt == 0) {
        Prompt();
    }
}

/* 命令可能把焦点切走（settings）；提示符只能画在 Shell 上 */
static void ConsolePromptAfterCommand(int FromSerial) {
    if (ConsoleJobConsumeSkipAfterCommand()) {
        return;
    }
    if (gWaitPrompt != 0 || ConsolePromptSuspended() || ConsoleJobPromptPending()) {
        return;
    }
    if (HalConsoleOnly() || GuiShellAcceptsInput() || FromSerial) {
        Prompt();
        return;
    }
    /* 焦点在 Settings 等：标记 Shell 待补提示符，点回 Shell 时再画 */
    GuiShellRequestPrompt();
}

/* 处理回车：执行命令并重新显示提示符 */
void ConsoleOnEnterEx(int FromSerial) {
    int Ensured;

    /* 用户 ELF 占 stdin：误入勿当 shell 命令（chat> HI → unknown） */
    if (ConsoleStdinUserHold()) {
        gLen = 0;
        return;
    }
    Ensured = ConsoleEnsureShell(FromSerial);
    if (Ensured == 0) {
        return;
    }
    if (Ensured == 2) {
        /* 仅用 Enter 开窗：已有欢迎语+提示符，勿再当空命令执行 */
        return;
    }
    /* listen 挂起时：空回车忽略并防旧命令残留 */
    if (ConsolePromptSuspended() && gLen == 0) {
        return;
    }
    ConsoleWrite("\n");
    /* 整命令 Present 合并：见 PR-G-shell-present */
    GuiPresentDeferPush();
    ConsoleHistApplyNavToLine();
    ConsoleHistPushLine();
    ConsoleCmdOutBegin();
    ConsoleRunLine();
    ConsoleCmdOutEnd();
    GuiPresentDeferPop();
    gLen = 0;
    ConsolePromptAfterCommand(FromSerial);
}

void ConsoleOnEnter(void) {
    ConsoleOnEnterEx(0);
}
