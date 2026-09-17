/*
 * Console.c — Shell 绘制 / 提示符 / 输入（PR-S-console-split-2）
 *
 * 命令表与别名见 ConsoleCmd.c；行缓冲见 ConsoleScroll.c。
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

char gLine[LINE_MAX];
int gLen;
int gWaitPrompt;
int gPromptSuspend;
int gAtLineStart = 1;

void ConsoleDrawString(const char *Text, UINT32 Color) {
    UINT32 X0;
    UINT32 Y0;
    UINT32 X1;
    UINT32 Y1;
    UINT32 L;
    UINT32 T;
    UINT32 R;
    UINT32 B;
    UINT32 Cx;
    UINT32 Cy;
    UINT32 Cw;
    UINT32 Ch;
    UINT32 Bg;

    GuiFrameBufferBegin();
    GuiFocusApplyClip();
    HalConsoleGetTextCursor(&X0, &Y0);
    HalConsoleDrawString(Text, Color);
    HalConsoleGetTextCursor(&X1, &Y1);
    /*
     * 换行/滚屏后 X 回到行首：若仍用 (X0,X1) 轴对齐包围盒，
     * max(X0,X1)+Advance 只盖住行首约一字，Compose 会把同行其余字盖掉 →「吞字」。
     */
    if (Y1 != Y0 || X1 < X0) {
        if (GuiFocusClient(&Cx, &Cy, &Cw, &Ch, &Bg) && Cw > 0 && Ch > 0) {
            if (Y1 < Y0) {
                /* 滚屏：整客户区 */
                GuiBackupSyncRect(Cx, Cy, Cw, Ch);
            } else {
                T = Y0 < Y1 ? Y0 : Y1;
                B = (Y0 > Y1 ? Y0 : Y1) + FontAdvanceY();
                if (B > Cy + Ch) {
                    B = Cy + Ch;
                }
                if (T < Cy) {
                    T = Cy;
                }
                if (B > T) {
                    GuiBackupSyncRect(Cx, T, Cw, B - T);
                }
            }
        }
    } else {
        L = X0 < X1 ? X0 : X1;
        T = Y0;
        R = (X0 > X1 ? X0 : X1) + FontAdvanceX();
        B = Y0 + FontAdvanceY();
        if (R > L && B > T) {
            GuiBackupSyncRect(L, T, R - L, B - T);
        }
    }
    GuiFocusSyncCursor();
    GuiFrameBufferEnd();
}

void ConsoleDrawChar(char C, UINT32 Color) {
    UINT32 X;
    UINT32 Y;

    GuiFrameBufferBegin();
    GuiFocusApplyClip();
    HalConsoleGetTextCursor(&X, &Y);
    HalConsoleDrawChar(C, Color);
    GuiBackupSyncRect(X, Y, FontCellW(), FontCellH());
    GuiFocusSyncCursor();
    /* PR-G-shell-present：打字回显合并 Present（ShellTask 轮询末刷） */
    GuiPresentShellEchoMark();
    GuiFrameBufferEnd();
}

/* 同时输出到串口与屏幕（帧缓冲未就绪时只写串口） */
void ConsoleWrite(const char *Text) {
    const char *P;

    if (Text == 0) {
        return;
    }
    HalConsoleWriteSerial(Text);
    for (P = Text; *P; P++) {
        gAtLineStart = (*P == '\n');
    }
    if (!HalConsoleVideoReady()) {
        return;
    }
    /*
     * 仅 Shell 客户区接受控制台绘制。焦点在 USER/Settings/Files 时若仍画 FB，
     * 会与 GuiDemo 标签等叠字，并污染用户窗备份（透视/花屏）。
     */
    if (GuiFocusKind() != GUI_WIN_SHELL) {
        /* 仍记入行缓冲，便于切回 Shell 滚轮看到近期输出 */
        ConsoleSbFeed(Text);
        return;
    }
    ConsoleSbEnsureLive();
    ConsoleSbFeed(Text);
    ConsoleDrawString(Text, COLOR_WHITE);
}

/* 按长度输出（SYS_WRITE 用；UTF-8 按码点画，不因中间的 NUL 截断） */
void ConsoleWriteLen(const char *Data, UINTN Len) {
    UINTN i;

    if (Data == 0 || Len == 0) {
        return;
    }
    i = 0;
    while (i < Len) {
        UINT32 Cp;
        UINTN N;
        char Tmp[5];
        UINTN k;

        if (Data[i] == '\n') {
            ConsoleWrite("\n");
            i++;
            continue;
        }
        if (Data[i] == '\0') {
            i++;
            continue;
        }
        /* 非法/截断 UTF-8：跳过单字节，避免死循环 */
        if (i + 4 > Len) {
            /* 剩余可能不足完整序列；尽量解，失败则跳过 */
        }
        N = Utf8Decode(Data + i, &Cp);
        if (N == 0 || i + N > Len) {
            i++;
            continue;
        }
        if (Cp < 32 || Cp == 127) {
            i += N;
            continue;
        }
        for (k = 0; k < N && k < sizeof(Tmp) - 1; k++) {
            Tmp[k] = Data[i + k];
        }
        Tmp[k] = 0;
        ConsoleWrite(Tmp);
        i += N;
    }
}

/* 输出 32 位十六进制 */
void ConsoleWriteHex32(UINT32 Value) {
    char Buf[12];
    HalSerialFormatHex(Buf, Value, 8);
    ConsoleWrite(Buf);
}

/* 输出 64 位十六进制 */
void ConsoleWriteHex64(UINT64 Value) {
    char Buf[20];
    HalSerialFormatHex(Buf, Value, 16);
    ConsoleWrite(Buf);
}

/* 显示 Shell 提示符 toyos>（清空输入行；若当前不在行首则先换行） */
static void Prompt(void) {
    gLen = 0;
    if (!gAtLineStart) {
        ConsoleWrite("\n");
    }
    HalConsoleWriteSerial("toyos> ");
    gAtLineStart = 0;
    if (HalConsoleVideoReady() && GuiFocusKind() == GUI_WIN_SHELL) {
        ConsoleSbEnsureLive();
        ConsoleSbFeed("toyos> ");
        ConsoleDrawString("toyos> ", COLOR_CYAN);
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
            ConsoleWrite(LocStr(MSG_CON_WELCOME));
            ConsoleWrite("\n");
            Prompt();
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
        GuiFocusHome();
        ConsoleWrite(LocStr(MSG_CON_WELCOME));
        ConsoleWrite("\n");
        Prompt();
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
