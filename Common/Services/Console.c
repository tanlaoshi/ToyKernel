/*
 * Console.c — 简易命令行 Shell
 *
 * 维护输入行缓冲与命令表，解析空格分隔参数后分发给 Handler。
 * 内置 help / clear / echo；其他模块通过 ConsoleRegister 扩展命令。
 */
#include "Console.h"
#include "Video.h"
#include "UI.h"
#include "Serial.h"
#include "Gui.h"

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
    GuiFrameBufferBegin();
    GuiFocusApplyClip();
    VideoDrawString(Text, Color);
    GuiFocusSyncCursor();
    GuiFrameBufferEnd();
}

static void ConsoleDrawChar(char C, UINT32 Color) {
    GuiFrameBufferBegin();
    GuiFocusApplyClip();
    VideoDrawChar(C, Color);
    GuiFocusSyncCursor();
    GuiFrameBufferEnd();
}

/* 同时输出到串口与屏幕（帧缓冲未就绪时只写串口） */
void ConsoleWrite(const char *Text) {
    UINT32 W;
    UINT32 H;

    SerialWrite(Text);
    VideoGetSize(&W, &H);
    if (W == 0 || H == 0) {
        return;
    }
    ConsoleDrawString(Text, COLOR_WHITE);
}

/* 输出 32 位十六进制 */
void ConsoleHex32(UINT32 Value) {
    char Buf[12];
    SerialHexFormat(Buf, Value, 8);
    ConsoleWrite(Buf);
}

/* 输出 64 位十六进制 */
void ConsoleHex64(UINT64 Value) {
    char Buf[20];
    SerialHexFormat(Buf, Value, 16);
    ConsoleWrite(Buf);
}

/* 显示 Shell 提示符 toyos>（清空输入行） */
static void Prompt(void) {
    gLen = 0;
    SerialWrite("toyos> ");
    ConsoleDrawString("toyos> ", COLOR_CYAN);
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
    /* 客户区已有终端输出时只恢复输入行，不重画提示符（避免 toyos> toyos>） */
    if (GuiConsoleHasDisplay()) {
        GuiConsoleMarkPrompt();
        return;
    }
    if (GuiConsoleNeedsPrompt()) {
        SerialWrite("\n");
        GuiFocusHome();
        Prompt();
        GuiConsoleMarkPrompt();
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

/* 初始化 Shell：打印欢迎语、显示提示符（内置命令须先由 InitConsole 注册） */
void ConsoleInit(void) {
    gLen = 0;
    gWaitPrompt = 0;
    GuiFocusHome();
    ConsoleWrite("ToyOS console. Type help.\n");
    Prompt();
    GuiConsoleMarkPrompt();
}

/* 处理可打印字符输入 */
void ConsoleOnChar(char C) {
    if (!GuiShellAcceptsInput()) {
        return;
    }
    if (C < 32 || C > 126) {
        return;
    }
    if (gLen >= LINE_MAX - 1) {
        return;
    }
    gLine[gLen++] = C;
    char Buf[2] = {C, 0};
    SerialWrite(Buf);
    ConsoleDrawChar(C, COLOR_WHITE);
}

/* 处理退格键 */
void ConsoleOnBackspace(void) {
    if (!GuiShellAcceptsInput()) {
        return;
    }
    if (gLen <= 0) {
        return;
    }
    gLen--;
    GuiFrameBufferBegin();
    GuiFocusApplyClip();
    VideoEraseLastChar();
    GuiFocusSyncCursor();
    GuiFrameBufferEnd();
    SerialWrite("\b \b");
}

/* 处理回车：执行命令并重新显示提示符 */
void ConsoleOnEnter(void) {
    if (!GuiShellAcceptsInput()) {
        return;
    }
    ConsoleWrite("\n");
    RunLine();
    if (gWaitPrompt == 0) {
        Prompt();
    }
}
