/*
 * ConsoleInput.c — 按键、回车与串口壳
 * 核心：Console.c
 */
#include "Console.h"
#include "ConsolePriv.h"
#include "UI.h"
#include "Hal.h"
#include "Gui.h"
#include "Font.h"
#include "SettingsUi.h"
#include "Locale.h"
#include "HIDKeyboard.h"
#include "LibWrite.h"
#include "ShellCommands.h"

/*
 * 空桌面按键自动开 Shell。
 * 返回：0 失败；1 已有可输入 Shell；2 刚打开（调用方应吞掉触发键，勿写入行缓冲）。
 */
static int ConsoleEnsureShell(void) {
    /* PR-B1：HalConsoleOnly — 串口子集不要求 GUI Shell 窗 */
    if (HalConsoleOnly()) {
        return 1;
    }
    if (GuiShellAcceptsInput()) {
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

/* 处理可打印字符输入 */
void ConsoleOnChar(char C) {
    int Ensured;

    Ensured = ConsoleEnsureShell();
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
    ConsoleSbEnsureLive();
    gLine[gLen++] = C;
    ConsoleSbFeedChar(C);
    HalConsolePutChar(C);
    if (HalConsoleVideoReady()) {
        ConsoleDrawChar(C, COLOR_WHITE);
    }
}

/* 处理退格键 */
void ConsoleOnBackspace(void) {
    if (!HalConsoleOnly() && !GuiShellAcceptsInput()) {
        return;
    }
    if (gLen <= 0) {
        return;
    }
    ConsoleSbEnsureLive();
    gLen--;
    ConsoleSbBackspace();
    if (HalConsoleVideoReady()) {
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
static void ConsolePromptAfterCommand(void) {
    if (gWaitPrompt != 0 || ConsolePromptSuspended()) {
        return;
    }
    if (HalConsoleOnly() || GuiShellAcceptsInput()) {
        Prompt();
        return;
    }
    /* 焦点在 Settings 等：标记 Shell 待补提示符，点回 Shell 时再画 */
    GuiShellRequestPrompt();
}

/* 处理回车：执行命令并重新显示提示符 */
void ConsoleOnEnter(void) {
    int Ensured;

    Ensured = ConsoleEnsureShell();
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
    ConsoleRunLine();
    GuiPresentDeferPop();
    gLen = 0;
    ConsolePromptAfterCommand();
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
            char C = HalSerialReadChar();
            if (C == '\r' || C == '\n') {
                ConsoleOnEnter();
            } else if (C == '\b' || C == 127) {
                ConsoleOnBackspace();
            } else if (C == 3) {
                ShellOnInterrupt();
            } else {
                ConsoleOnChar(C);
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
