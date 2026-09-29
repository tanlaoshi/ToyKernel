/*
 * ConsoleInput.c — 按键、回车与串口壳
 * 核心：Console.c
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
#include "HIDKeyboard.h"
#include "LibWrite.h"
#include "ShellCommands.h"

/*
 * 空桌面按键自动开 Shell。
 * FromSerial：串口始终可敲（不抢 GUI 焦点、不开窗）；键盘仍受焦点约束。
 * 返回：0 失败；1 已可输入；2 刚打开（调用方应吞掉触发键，勿写入行缓冲）。
 */
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
         * 串口：SNAKE 等占焦点时也会丢键（旧逻辑）。
         * 有 Shell 则置顶；没有则开一个——勿静默收键却不给窗。
         */
        if (FocusExistingKind(GUI_WIN_SHELL, 0, "shell-serial") >= 0) {
            return 1;
        }
        if (GuiOpenShell() < 0) {
            HalConsoleWriteSerial("shell: no free window\n");
            return 1; /* 仍收串口到行缓冲 */
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

    Ensured = ConsoleEnsureShell(FromSerial);
    if (Ensured == 0) {
        return;
    }
    if (Ensured == 2) {
        /* 仅用 Enter 开窗：已有欢迎语+提示符，勿再当空命令执行 */
        return;
    }
    /*
     * listen 等挂起提示期间：空回车只应忽略。若不在此清空 gLen，
     * Prompt() 不会跑，旧命令仍留在缓冲里，再按 Enter 会重跑并叠加 Suspend。
     */
    if (ConsolePromptSuspended() && gLen == 0) {
        return;
    }
    ConsoleWrite("\n");
    /* help/ls 等大量 ConsoleWrite：真机逐行 Present 极卡，整命令结束再刷一次。
     * PR-G-shell-present 只合并打字回显；本 Defer 语义保持不变。 */
    GuiPresentDeferPush();
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

/* PR-A9/V3：virt 串口 + virtio-input 键盘（Common 调 Hal*；不进 HAL） */
/* PR-A13：HalCpuHalt 可被 timer IRQ 唤醒，不再空转 HalTimerPoll */
void ConsoleSerialRun(void) {
    static HAL_KEYBOARD_REPORT Prev;
    HAL_KEYBOARD_REPORT Report;
    /* PR-B3：真机命令行靶非 virt 形状；文案跟 HalPlatformIsVirtSerialConsole */
    if (HalPlatformIsVirtSerialConsole()) {
        HalConsoleWriteSerial(
            "virt: serial shell (help/mem/ps/halt; kbd via virtio-input)\n");
    } else {
        HalConsoleWriteSerial("serial shell (help/mem/ps/halt)\n");
    }
    Prompt();
    for (;;) {
        HalCpuHalt();
        HalInputPoll();
        if (HalSerialDataReady()) {
            static int SkipLf;
            char C = HalSerialReadChar();
            if (C == '\r') {
                SkipLf = 1;
                ConsoleOnEnterEx(1);
            } else if (C == '\n') {
                if (SkipLf) {
                    SkipLf = 0;
                } else {
                    ConsoleOnEnterEx(1);
                }
            } else {
                SkipLf = 0;
                if (C == '\b' || C == 127) {
                    ConsoleOnBackspaceEx(1);
                } else if (C == 3) {
                    ShellOnInterrupt();
                } else {
                    ConsoleOnCharEx(C, 1);
                }
            }
        }
        while (HalKeyboardDequeue(&Report)) {
            int i;
            for (i = 0; i < 6; i++) {
                UINT8 Key = Report.KeyCode[i];
                int Was = 0;
                int j;
                if (Key == 0) {
                    continue;
                }
                for (j = 0; j < 6; j++) {
                    if (Prev.KeyCode[j] == Key) {
                        Was = 1;
                        break;
                    }
                }
                if (Was) {
                    continue;
                }
                if (Key == 0x28) { /* ENTER */
                    ConsoleOnEnter();
                } else if (Key == 0x2A) { /* BACKSPACE */
                    ConsoleOnBackspace();
                } else if (Key == HID_KEY_C &&
                           (Report.ModifierKeys & (HID_MOD_LCTRL | HID_MOD_RCTRL)) != 0) {
                    ShellOnInterrupt();
                } else {
                    char C = HIDKeyCodeToASCII(Key, Report.ModifierKeys);
                    if (C) {
                        ConsoleOnChar(C);
                    }
                }
            }
            Prev = Report;
        }
        HalCpuRelax();
    }
}
