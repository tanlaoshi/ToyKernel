/*
 * Console.c — 简易命令行 Shell
 *
 * 维护输入行缓冲与命令表，解析空格分隔参数后分发给 Handler。
 * 内置 help / clear / echo；其他模块通过 ConsoleRegister 扩展命令。
 *
 * 输出经 HalConsole 门面：串口（HalConsoleWriteSerial）与帧缓冲（HalConsoleDraw*，
 * 由本文件配合 Gui 做 clip/备份同步）；不直接调用 HalSerial/HalVideo。
 */
#include "Console.h"
#include "UI.h"
#include "Hal.h"
#include "Gui.h"
#include "Font.h"
#include "SettingsUi.h"
#include "Locale.h"
#include "HIDKeyboard.h"
#include "KernelModules.h"

#define LINE_MAX 128
#define ARG_MAX  8
#define CMD_MAX  32

typedef struct {
    const char *Name;
    const char *Help;
    void (*Handler)(int Argc, char **Argv);
} COMMAND;

static COMMAND gCommands[CMD_MAX];
static int gCmdCount;
static char gLine[LINE_MAX];
static int gLen;
static int gWaitPrompt;
static int gPromptSuspend;
static int gAtLineStart = 1;

/* 比较两个 C 字符串是否相等 */
static int StrEq(const char *A, const char *B) {
    while (*A && *B) {
        if (*A != *B) {
            return 0;
        }
        A++;
        B++;
    }
    return *A == *B;
}

/* 帧缓冲绘制：先擦掉光标 → 写字 → 再画回光标 */
static void ConsoleDrawString(const char *Text, UINT32 Color) {
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

static void ConsoleDrawChar(char C, UINT32 Color) {
    UINT32 X;
    UINT32 Y;

    GuiFrameBufferBegin();
    GuiFocusApplyClip();
    HalConsoleGetTextCursor(&X, &Y);
    HalConsoleDrawChar(C, Color);
    GuiBackupSyncRect(X, Y, FontCellW(), FontCellH());
    GuiFocusSyncCursor();
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
void ConsoleHex32(UINT32 Value) {
    char Buf[12];
    HalSerialHexFormat(Buf, Value, 8);
    ConsoleWrite(Buf);
}

/* 输出 64 位十六进制 */
void ConsoleHex64(UINT64 Value) {
    char Buf[20];
    HalSerialHexFormat(Buf, Value, 16);
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
    if (HalConsoleVideoReady()) {
        ConsoleDrawString("toyos> ", COLOR_CYAN);
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

/* 内置命令：列出所有已注册命令 */
static void CommandHelp(int Argc, char **Argv) {
    (void)Argc;
    (void)Argv;
    ConsoleWrite("commands:\n");
    for (int i = 0; i < gCmdCount; i++) {
        ConsoleWrite("  ");
        ConsoleWrite(gCommands[i].Name);
        ConsoleWrite("  ");
        ConsoleWrite(gCommands[i].Help);
        ConsoleWrite("\n");
    }
}

/* 内置命令：清屏（仅当前焦点窗客户区；提示符由 ConsoleOnEnter 统一显示） */
static void CommandClear(int Argc, char **Argv) {
    (void)Argc;
    (void)Argv;
    gLen = 0;
    gAtLineStart = 1;
    GuiFocusClearClient();
}

/* 内置命令：回显参数 */
static void CommandEcho(int Argc, char **Argv) {
    for (int i = 1; i < Argc; i++) {
        if (i > 1) {
            ConsoleWrite(" ");
        }
        ConsoleWrite(Argv[i]);
    }
    ConsoleWrite("\n");
}

/* 注册一条 Shell 命令（名称、帮助、处理函数） */
void ConsoleRegister(const char *Name, const char *Help,
                     void (*Handler)(int Argc, char **Argv)) {
    if (gCmdCount >= CMD_MAX || Name == 0 || Handler == 0) {
        return;
    }
    gCommands[gCmdCount].Name = Name;
    gCommands[gCmdCount].Help = Help ? Help : "";
    gCommands[gCmdCount].Handler = Handler;
    gCmdCount++;
}

/* 解析当前输入行并执行匹配的命令 */
static void RunLine(void) {
    char Buf[LINE_MAX];
    char *Argv[ARG_MAX];
    int Argc = 0;
    int i;

    if (gLen >= LINE_MAX) {
        gLen = LINE_MAX - 1;
    }
    for (i = 0; i < gLen; i++) {
        Buf[i] = gLine[i];
    }
    Buf[gLen] = 0;

    i = 0;
    while (Buf[i] && Argc < ARG_MAX) {
        while (Buf[i] == ' ') {
            i++;
        }
        if (!Buf[i]) {
            break;
        }
        Argv[Argc++] = &Buf[i];
        while (Buf[i] && Buf[i] != ' ') {
            i++;
        }
        if (Buf[i] == ' ') {
            Buf[i++] = 0;
        }
    }

    if (Argc == 0) {
        return;
    }
    for (i = 0; i < gCmdCount; i++) {
        if (StrEq(Argv[0], gCommands[i].Name)) {
            gCommands[i].Handler(Argc, Argv);
            return;
        }
    }
    ConsoleWrite("unknown: ");
    ConsoleWrite(Argv[0]);
    ConsoleWrite("  (help)\n");
}

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

/* 注册 help / clear / echo（须在 ShellCommands 之前调用） */
void ConsoleRegisterBuiltins(void) {
    ConsoleRegister("help", "list commands", CommandHelp);
    ConsoleRegister("clear", "clear screen", CommandClear);
    ConsoleRegister("echo", "print arguments", CommandEcho);
}

/* 将控制台输出限制在当前焦点窗口客户区内（不重置光标） */
void ConsoleBindFocus(void) {
    GuiFocusApply();
}

/* 初始化：无 Shell 时仅串口提示；开窗后由 ConsoleOnShellOpened 画欢迎语 */
void ConsoleInit(void) {
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
    GuiSetFocusWin(Idx);
    gLen = 0;
    gWaitPrompt = 0;
    gAtLineStart = 1;
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
        GuiSetFocusWin(Saved);
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
    /* 无 FB 的 virt 串口子集：不要求 GUI Shell 窗 */
    if (HalPlatformVirtConsole() && !KernelModulesVirtDesktop()) {
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
    ConsoleOnShellOpened();
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
    gLine[gLen++] = C;
    HalConsolePutChar(C);
    if (HalConsoleVideoReady()) {
        ConsoleDrawChar(C, COLOR_WHITE);
    }
}

/* 处理退格键 */
void ConsoleOnBackspace(void) {
    if (!(HalPlatformVirtConsole() && !KernelModulesVirtDesktop()) &&
        !GuiShellAcceptsInput()) {
        return;
    }
    if (gLen <= 0) {
        return;
    }
    gLen--;
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
        GuiFrameBufferEnd();
    }
    HalConsoleBackspaceSerial();
}

void ConsoleCancelInput(void) {
    if ((!(HalPlatformVirtConsole() && !KernelModulesVirtDesktop()) &&
         !GuiShellAcceptsInput()) ||
        gLen <= 0) {
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

/* 命令可能把焦点切走（settings）；提示符只能画在 Shell 上 */
static void ConsolePromptAfterCommand(void) {
    if (gWaitPrompt != 0 || ConsolePromptSuspended()) {
        return;
    }
    if ((HalPlatformVirtConsole() && !KernelModulesVirtDesktop()) ||
        GuiShellAcceptsInput()) {
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
    ConsoleWrite("\n");
    RunLine();
    ConsolePromptAfterCommand();
}

/* PR-A9/V3：virt 串口 + virtio-input 键盘（Common 调 Hal*；不进 HAL） */
/* PR-A13：HalCpuHalt 可被 timer IRQ 唤醒，不再空转 HalTimerPoll */
void ConsoleSerialRun(void) {
    static HAL_KEYBOARD_REPORT Prev;
    HAL_KEYBOARD_REPORT Report;
    HalConsoleWriteSerial("virt: serial shell (help/mem/ps/halt; kbd via virtio-input)\n");
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
                ConsoleCancelInput();
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
