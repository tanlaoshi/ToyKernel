/*
 * Console.c — 简易命令行 Shell
 *
 * 维护输入行缓冲与命令表，解析空格分隔参数后分发给 Handler。
 * 内置 help / clear / echo；其他模块通过 ConsoleRegister / Register2 扩展。
 * PR-C1：一级/二级分发 + 别名（别名不占 CMD 槽）。
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
#include "LibWrite.h"
#include "Db.h"
#include "ShellCommands.h"

#define LINE_MAX 128
#define ARG_MAX  8
/* builtins+Shell+FS+Db+lwip 已超 32；满表时 ConsoleRegister 静默失败会丢末尾命令（如 lwip） */
#define CMD_MAX  48
#define SUB_MAX  12
#define ALIAS_MAX 80
#define USER_ALIAS_MAX 16
#define USER_ALIAS_NAME 20
#define USER_ALIAS_WORD 16
/* PR-I2 补：Shell 行缓冲滚动（替代破坏性像素平移） */
#define SB_LINES 64
#define SB_COLS  120

typedef struct {
    const char *Name;
    const char *Help;
    void (*Handler)(int Argc, char **Argv);
} COMMAND_SUB;

typedef struct {
    const char *Name;
    const char *Help;
    void (*Handler)(int Argc, char **Argv); /* 无二级时使用；有二级则为 NULL */
    COMMAND_SUB Subs[SUB_MAX];
    int SubCount;
} COMMAND;

typedef struct {
    const char *Alias;
    const char *Level1;
    const char *Level2; /* NULL = 仅改写一级 */
} COMMAND_ALIAS;

/* PR-C3 补：用户自定义别名（可覆盖同名内置别名；落盘 al.<name>） */
typedef struct {
    char Alias[USER_ALIAS_NAME];
    char Level1[USER_ALIAS_WORD];
    char Level2[USER_ALIAS_WORD]; /* [0]==0 表示无二级 */
} USER_ALIAS;

static COMMAND gCommands[CMD_MAX];
static int gCmdCount;
static COMMAND_ALIAS gAliases[ALIAS_MAX];
static int gAliasCount;
static USER_ALIAS gUserAliases[USER_ALIAS_MAX];
static int gUserAliasCount;
static char gLine[LINE_MAX];
static int gLen;
static int gWaitPrompt;
static int gPromptSuspend;
static int gAtLineStart = 1;

/* 已完成行环形缓冲；gViewOff>0 表示向上查看历史 */
static char gSb[SB_LINES][SB_COLS];
static int gSbCount;
static int gSbNext;
static int gViewOff;
static char gAcc[SB_COLS];
static int gAccLen;

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

static void ConsoleDrawString(const char *Text, UINT32 Color);
static void ConsoleDrawChar(char C, UINT32 Color);

static void ConsoleSbReset(void) {
    gSbCount = 0;
    gSbNext = 0;
    gViewOff = 0;
    gAccLen = 0;
    gAcc[0] = 0;
}

static void ConsoleSbPushLine(void) {
    int i;

    for (i = 0; i < SB_COLS - 1 && i < gAccLen; i++) {
        gSb[gSbNext][i] = gAcc[i];
    }
    gSb[gSbNext][i] = 0;
    gSbNext = (gSbNext + 1) % SB_LINES;
    if (gSbCount < SB_LINES) {
        gSbCount++;
    }
    gAccLen = 0;
    gAcc[0] = 0;
}

static void ConsoleSbFeedChar(char C) {
    if (C == '\n') {
        ConsoleSbPushLine();
        return;
    }
    if (C < 32 || C == 127) {
        return;
    }
    if (gAccLen + 1 >= SB_COLS) {
        ConsoleSbPushLine();
    }
    if (gAccLen + 1 < SB_COLS) {
        gAcc[gAccLen++] = C;
        gAcc[gAccLen] = 0;
    }
}

static void ConsoleSbFeed(const char *Text) {
    if (!Text) {
        return;
    }
    while (*Text) {
        ConsoleSbFeedChar(*Text++);
    }
}

static void ConsoleSbBackspace(void) {
    if (gAccLen > 0) {
        gAccLen--;
        gAcc[gAccLen] = 0;
    }
}

static const char *ConsoleSbLine(int OldestIndex) {
    int Idx;

    if (OldestIndex < 0 || OldestIndex >= gSbCount) {
        return "";
    }
    Idx = gSbNext - gSbCount + OldestIndex;
    while (Idx < 0) {
        Idx += SB_LINES;
    }
    Idx %= SB_LINES;
    return gSb[Idx];
}

static void ConsoleSbPaint(void) {
    UINT32 Cx;
    UINT32 Cy;
    UINT32 Cw;
    UINT32 Ch;
    UINT32 Bg;
    UINT32 LineH;
    int Vis;
    int Start;
    int End;
    int i;

    if (!GuiShellAcceptsInput()) {
        return;
    }
    if (!GuiFocusClient(&Cx, &Cy, &Cw, &Ch, &Bg) || Ch == 0) {
        return;
    }
    LineH = FontAdvanceY();
    if (LineH < 8) {
        LineH = 16;
    }
    Vis = (int)(Ch / LineH);
    if (Vis < 1) {
        Vis = 1;
    }

    GuiFocusClearClient();
    GuiFocusHome();

    End = gSbCount - gViewOff;
    if (End < 0) {
        End = 0;
    }
    if (End > gSbCount) {
        End = gSbCount;
    }
    Start = End - Vis;
    if (gViewOff == 0 && gAccLen > 0) {
        /* 末行留给当前未完成行 */
        Start = End - (Vis - 1);
    }
    if (Start < 0) {
        Start = 0;
    }

    for (i = Start; i < End; i++) {
        const char *L = ConsoleSbLine(i);
        /* 历史行里的提示符也保持青色 */
        if (L[0] == 't' && L[1] == 'o' && L[2] == 'y' && L[3] == 'o' &&
            L[4] == 's' && L[5] == '>' && L[6] == ' ') {
            ConsoleDrawString("toyos> ", COLOR_CYAN);
            if (L[7]) {
                ConsoleDrawString(L + 7, COLOR_WHITE);
            }
        } else {
            ConsoleDrawString(L, COLOR_WHITE);
        }
        ConsoleDrawString("\n", COLOR_WHITE);
    }
    if (gViewOff == 0 && gAccLen > 0) {
        if (gAcc[0] == 't' && gAcc[1] == 'o' && gAcc[2] == 'y' && gAcc[3] == 'o' &&
            gAcc[4] == 's' && gAcc[5] == '>' && gAcc[6] == ' ') {
            ConsoleDrawString("toyos> ", COLOR_CYAN);
            if (gAcc[7]) {
                ConsoleDrawString(gAcc + 7, COLOR_WHITE);
            }
        } else {
            ConsoleDrawString(gAcc, COLOR_WHITE);
        }
    }

    GuiFocusSave();
    HalVideoClearClip();
    GuiBackupFocusWindow();
}

/* 若正在看历史，先回到底部再继续输出/输入 */
static void ConsoleSbEnsureLive(void) {
    if (gViewOff == 0) {
        return;
    }
    gViewOff = 0;
    ConsoleSbPaint();
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

/* 内置命令：列出所有已注册命令（一级分组；二级缩进；括号列别名） */
static void HelpWriteAliasesFor(const char *Level1, const char *Level2) {
    int First = 1;
    int i;

    for (i = 0; i < gUserAliasCount; i++) {
        if (gUserAliases[i].Alias[0] != 0 &&
            StrEq(gUserAliases[i].Level1, Level1)) {
            if (Level2 == 0) {
                if (gUserAliases[i].Level2[0] != 0) {
                    continue;
                }
            } else {
                if (gUserAliases[i].Level2[0] == 0 ||
                    !StrEq(gUserAliases[i].Level2, Level2)) {
                    continue;
                }
            }
            if (First) {
                ConsoleWrite(" (");
                First = 0;
            } else {
                ConsoleWrite(", ");
            }
            ConsoleWrite(gUserAliases[i].Alias);
            ConsoleWrite("*");
        }
    }
    for (i = 0; i < gAliasCount; i++) {
        if (!StrEq(gAliases[i].Level1, Level1)) {
            continue;
        }
        if (Level2 == 0) {
            if (gAliases[i].Level2 != 0) {
                continue;
            }
        } else {
            if (gAliases[i].Level2 == 0 || !StrEq(gAliases[i].Level2, Level2)) {
                continue;
            }
        }
        if (First) {
            ConsoleWrite(" (");
            First = 0;
        } else {
            ConsoleWrite(", ");
        }
        ConsoleWrite(gAliases[i].Alias);
    }
    if (!First) {
        ConsoleWrite(")");
    }
}

static void CommandHelp(int Argc, char **Argv) {
    int i;
    int s;

    (void)Argc;
    (void)Argv;
    ConsoleWrite("commands:\n");
    for (i = 0; i < gCmdCount; i++) {
        ConsoleWrite("  ");
        ConsoleWrite(gCommands[i].Name);
        if (gCommands[i].SubCount > 0) {
            ConsoleWrite("  ");
            ConsoleWrite(gCommands[i].Help[0] ? gCommands[i].Help : "(family)");
            HelpWriteAliasesFor(gCommands[i].Name, 0);
            ConsoleWrite("\n");
            for (s = 0; s < gCommands[i].SubCount; s++) {
                ConsoleWrite("    ");
                ConsoleWrite(gCommands[i].Subs[s].Name);
                ConsoleWrite("  ");
                ConsoleWrite(gCommands[i].Subs[s].Help);
                HelpWriteAliasesFor(gCommands[i].Name, gCommands[i].Subs[s].Name);
                ConsoleWrite("\n");
            }
        } else {
            ConsoleWrite("  ");
            ConsoleWrite(gCommands[i].Help);
            HelpWriteAliasesFor(gCommands[i].Name, 0);
            ConsoleWrite("\n");
        }
    }
}

/* 内置命令：清屏（仅当前焦点窗客户区；提示符由 ConsoleOnEnter 统一显示） */
static void CommandClear(int Argc, char **Argv) {
    (void)Argc;
    (void)Argv;
    gLen = 0;
    gAtLineStart = 1;
    ConsoleSbReset();
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

static int FindCommandIndex(const char *Name) {
    int i;

    for (i = 0; i < gCmdCount; i++) {
        if (StrEq(gCommands[i].Name, Name)) {
            return i;
        }
    }
    return -1;
}

static void PrintLevel1Usage(const COMMAND *Cmd) {
    int s;

    ConsoleWrite("usage: ");
    ConsoleWrite(Cmd->Name);
    ConsoleWrite(" <");
    for (s = 0; s < Cmd->SubCount; s++) {
        if (s > 0) {
            ConsoleWrite("|");
        }
        ConsoleWrite(Cmd->Subs[s].Name);
    }
    ConsoleWrite("> ...\n");
}

/* 注册一条仅一级的 Shell 命令（名称、帮助、处理函数） */
void ConsoleRegister(const char *Name, const char *Help,
                     void (*Handler)(int Argc, char **Argv)) {
    int Idx;

    if (Name == 0 || Handler == 0) {
        return;
    }
    Idx = FindCommandIndex(Name);
    if (Idx >= 0) {
        /* 允许给已有二级族补默认 Handler（如 list 列目录） */
        gCommands[Idx].Help = Help ? Help : gCommands[Idx].Help;
        gCommands[Idx].Handler = Handler;
        return;
    }
    if (gCmdCount >= CMD_MAX) {
        HalConsoleWriteSerial("console: CMD_MAX full, drop ");
        HalConsoleWriteSerial(Name);
        HalConsoleWriteSerial("\n");
        return;
    }
    gCommands[gCmdCount].Name = Name;
    gCommands[gCmdCount].Help = Help ? Help : "";
    gCommands[gCmdCount].Handler = Handler;
    gCommands[gCmdCount].SubCount = 0;
    gCmdCount++;
}

/* 注册一级+二级；同一一级可多次调用追加二级 */
void ConsoleRegister2(const char *Level1, const char *Level2, const char *Help,
                      void (*Handler)(int Argc, char **Argv)) {
    int Idx;
    int s;

    if (Level1 == 0 || Level2 == 0 || Handler == 0) {
        return;
    }
    Idx = FindCommandIndex(Level1);
    if (Idx < 0) {
        if (gCmdCount >= CMD_MAX) {
            HalConsoleWriteSerial("console: CMD_MAX full, drop ");
            HalConsoleWriteSerial(Level1);
            HalConsoleWriteSerial("\n");
            return;
        }
        Idx = gCmdCount;
        gCommands[Idx].Name = Level1;
        gCommands[Idx].Help = "";
        gCommands[Idx].Handler = 0;
        gCommands[Idx].SubCount = 0;
        gCmdCount++;
    }
    /* 可保留已有 L1 Handler 作默认（如 list 列目录 + list tasks） */

    for (s = 0; s < gCommands[Idx].SubCount; s++) {
        if (StrEq(gCommands[Idx].Subs[s].Name, Level2)) {
            gCommands[Idx].Subs[s].Help = Help ? Help : "";
            gCommands[Idx].Subs[s].Handler = Handler;
            return;
        }
    }
    if (gCommands[Idx].SubCount >= SUB_MAX) {
        HalConsoleWriteSerial("console: SUB_MAX full, drop ");
        HalConsoleWriteSerial(Level1);
        HalConsoleWriteSerial(" ");
        HalConsoleWriteSerial(Level2);
        HalConsoleWriteSerial("\n");
        return;
    }
    s = gCommands[Idx].SubCount;
    gCommands[Idx].Subs[s].Name = Level2;
    gCommands[Idx].Subs[s].Help = Help ? Help : "";
    gCommands[Idx].Subs[s].Handler = Handler;
    gCommands[Idx].SubCount++;
}

void ConsoleRegisterAlias(const char *CanonicalLevel1, const char *Alias) {
    if (CanonicalLevel1 == 0 || Alias == 0) {
        return;
    }
    if (gAliasCount >= ALIAS_MAX) {
        HalConsoleWriteSerial("console: ALIAS_MAX full\n");
        return;
    }
    gAliases[gAliasCount].Alias = Alias;
    gAliases[gAliasCount].Level1 = CanonicalLevel1;
    gAliases[gAliasCount].Level2 = 0;
    gAliasCount++;
}

void ConsoleRegisterAliasLine(const char *Alias, const char *Level1,
                              const char *Level2) {
    if (Alias == 0 || Level1 == 0 || Level2 == 0) {
        return;
    }
    if (gAliasCount >= ALIAS_MAX) {
        HalConsoleWriteSerial("console: ALIAS_MAX full\n");
        return;
    }
    gAliases[gAliasCount].Alias = Alias;
    gAliases[gAliasCount].Level1 = Level1;
    gAliases[gAliasCount].Level2 = Level2;
    gAliasCount++;
}

static void CopyWord(char *Dst, int DstMax, const char *Src) {
    int i = 0;

    if (DstMax <= 0) {
        return;
    }
    while (Src[i] && i < DstMax - 1) {
        Dst[i] = Src[i];
        i++;
    }
    Dst[i] = 0;
}

static int FindUserAliasIndex(const char *Name) {
    int i;

    for (i = 0; i < gUserAliasCount; i++) {
        if (StrEq(gUserAliases[i].Alias, Name)) {
            return i;
        }
    }
    return -1;
}

static int AliasTargetOk(const char *Level1, const char *Level2) {
    int Idx;
    int s;

    Idx = FindCommandIndex(Level1);
    if (Idx < 0) {
        return 0;
    }
    if (Level2 == 0 || Level2[0] == 0) {
        return 1;
    }
    for (s = 0; s < gCommands[Idx].SubCount; s++) {
        if (StrEq(gCommands[Idx].Subs[s].Name, Level2)) {
            return 1;
        }
    }
    /* 允许 list Apps 类：有默认 Handler 即可挂仅一级别名；二级须真实存在 */
    return 0;
}

static int UserAliasPersist(const char *Alias, const char *Level1,
                            const char *Level2, int Delete) {
    char Key[DB_KEY_MAX];
    char Val[DB_VAL_MAX];
    int i;
    int j;

    Key[0] = 'a';
    Key[1] = 'l';
    Key[2] = '.';
    i = 0;
    while (Alias[i] && i + 4 < DB_KEY_MAX) {
        Key[3 + i] = Alias[i];
        i++;
    }
    Key[3 + i] = 0;
    if (Delete) {
        return DbDelete(Key);
    }
    i = 0;
    while (Level1[i] && i + 1 < DB_VAL_MAX) {
        Val[i] = Level1[i];
        i++;
    }
    if (Level2 != 0 && Level2[0] != 0) {
        if (i + 2 >= DB_VAL_MAX) {
            return DB_INVAL;
        }
        Val[i++] = ' ';
        j = 0;
        while (Level2[j] && i + 1 < DB_VAL_MAX) {
            Val[i++] = Level2[j++];
        }
    }
    Val[i] = 0;
    return DbSet(Key, Val);
}

/* 返回 0 成功；负值失败（已打 Console 文案） */
static int UserAliasSet(const char *Alias, const char *Level1,
                        const char *Level2) {
    int Idx;
    const char *L2 = Level2;

    if (Alias == 0 || Alias[0] == 0 || Level1 == 0 || Level1[0] == 0) {
        ConsoleWrite("alias: bad name\n");
        return -1;
    }
    if (L2 != 0 && L2[0] == 0) {
        L2 = 0;
    }
    if (!AliasTargetOk(Level1, L2)) {
        ConsoleWrite("alias: unknown target\n");
        return -1;
    }
    Idx = FindUserAliasIndex(Alias);
    if (Idx < 0) {
        if (gUserAliasCount >= USER_ALIAS_MAX) {
            ConsoleWrite("alias: table full\n");
            return -1;
        }
        Idx = gUserAliasCount++;
    }
    CopyWord(gUserAliases[Idx].Alias, USER_ALIAS_NAME, Alias);
    CopyWord(gUserAliases[Idx].Level1, USER_ALIAS_WORD, Level1);
    if (L2) {
        CopyWord(gUserAliases[Idx].Level2, USER_ALIAS_WORD, L2);
    } else {
        gUserAliases[Idx].Level2[0] = 0;
    }
    if (UserAliasPersist(Alias, Level1, L2, 0) != DB_OK) {
        ConsoleWrite("alias: set (memory only; db save failed)\n");
    }
    return 0;
}

static int UserAliasRemove(const char *Alias) {
    int Idx;
    int i;

    Idx = FindUserAliasIndex(Alias);
    if (Idx < 0) {
        ConsoleWrite("alias: not found\n");
        return -1;
    }
    (void)UserAliasPersist(Alias, 0, 0, 1);
    for (i = Idx; i < gUserAliasCount - 1; i++) {
        gUserAliases[i] = gUserAliases[i + 1];
    }
    gUserAliasCount--;
    return 0;
}

static int UserAliasLoadCb(const char *Key, const char *Value, void *Ctx) {
    char Alias[USER_ALIAS_NAME];
    char L1[USER_ALIAS_WORD];
    char L2[USER_ALIAS_WORD];
    int i;
    int j;

    (void)Ctx;
    if (Key[0] != 'a' || Key[1] != 'l' || Key[2] != '.') {
        return 0;
    }
    CopyWord(Alias, USER_ALIAS_NAME, Key + 3);
    if (Alias[0] == 0 || Value == 0 || Value[0] == 0) {
        return 0;
    }
    i = 0;
    while (Value[i] && Value[i] != ' ' && i < USER_ALIAS_WORD - 1) {
        L1[i] = Value[i];
        i++;
    }
    L1[i] = 0;
    L2[0] = 0;
    if (Value[i] == ' ') {
        i++;
        j = 0;
        while (Value[i] && Value[i] != ' ' && j < USER_ALIAS_WORD - 1) {
            L2[j++] = Value[i++];
        }
        L2[j] = 0;
    }
    if (!AliasTargetOk(L1, L2[0] ? L2 : 0)) {
        return 0;
    }
    if (FindUserAliasIndex(Alias) >= 0) {
        return 0;
    }
    if (gUserAliasCount >= USER_ALIAS_MAX) {
        return 0;
    }
    CopyWord(gUserAliases[gUserAliasCount].Alias, USER_ALIAS_NAME, Alias);
    CopyWord(gUserAliases[gUserAliasCount].Level1, USER_ALIAS_WORD, L1);
    CopyWord(gUserAliases[gUserAliasCount].Level2, USER_ALIAS_WORD, L2);
    gUserAliasCount++;
    return 0;
}

void ConsoleUserAliasLoad(void) {
    gUserAliasCount = 0;
    (void)DbForEach(UserAliasLoadCb, 0);
}

/* alias / unalias（PR-C3 用户自定义；* 在 help 中标用户别名） */
static void CommandAlias(int Argc, char **Argv) {
    char Name[USER_ALIAS_NAME];
    char L1[USER_ALIAS_WORD];
    char L2[USER_ALIAS_WORD];
    const char *P;
    int i;
    int j;

    if (Argc < 2) {
        if (gUserAliasCount == 0) {
            ConsoleWrite("alias: (none)  usage: alias <name> <cmd> [sub]\n");
            ConsoleWrite("  or alias name=cmd [sub]; unalias <name>\n");
            return;
        }
        for (i = 0; i < gUserAliasCount; i++) {
            ConsoleWrite("  ");
            ConsoleWrite(gUserAliases[i].Alias);
            ConsoleWrite("=");
            ConsoleWrite(gUserAliases[i].Level1);
            if (gUserAliases[i].Level2[0]) {
                ConsoleWrite(" ");
                ConsoleWrite(gUserAliases[i].Level2);
            }
            ConsoleWrite("\n");
        }
        return;
    }

    /* alias name=l1 [l2] */
    P = Argv[1];
    i = 0;
    while (P[i] && P[i] != '=' && i < USER_ALIAS_NAME - 1) {
        Name[i] = P[i];
        i++;
    }
    Name[i] = 0;
    if (P[i] == '=') {
        P = P + i + 1;
        j = 0;
        while (P[j] && P[j] != ' ' && j < USER_ALIAS_WORD - 1) {
            L1[j] = P[j];
            j++;
        }
        L1[j] = 0;
        L2[0] = 0;
        if (P[j] == ' ') {
            P = P + j + 1;
            j = 0;
            while (P[j] && j < USER_ALIAS_WORD - 1) {
                L2[j] = P[j];
                j++;
            }
            L2[j] = 0;
        } else if (Argc >= 3) {
            CopyWord(L2, USER_ALIAS_WORD, Argv[2]);
        }
        if (L1[0] == 0) {
            ConsoleWrite("usage: alias name=cmd [sub]\n");
            return;
        }
        if (UserAliasSet(Name, L1, L2[0] ? L2 : 0) == 0) {
            ConsoleWrite("alias: set ");
            ConsoleWrite(Name);
            ConsoleWrite("\n");
        }
        return;
    }

    /* alias name l1 [l2] */
    if (Argc < 3) {
        ConsoleWrite("usage: alias <name> <cmd> [sub]\n");
        return;
    }
    CopyWord(Name, USER_ALIAS_NAME, Argv[1]);
    CopyWord(L1, USER_ALIAS_WORD, Argv[2]);
    L2[0] = 0;
    if (Argc >= 4) {
        CopyWord(L2, USER_ALIAS_WORD, Argv[3]);
    }
    if (UserAliasSet(Name, L1, L2[0] ? L2 : 0) == 0) {
        ConsoleWrite("alias: set ");
        ConsoleWrite(Name);
        ConsoleWrite("\n");
    }
}

static void CommandUnalias(int Argc, char **Argv) {
    if (Argc < 2) {
        ConsoleWrite("usage: unalias <name>\n");
        return;
    }
    if (UserAliasRemove(Argv[1]) == 0) {
        ConsoleWrite("alias: removed ");
        ConsoleWrite(Argv[1]);
        ConsoleWrite("\n");
    }
}

/* 解析当前输入行并执行匹配的命令 */
static void RunLine(void) {
    char Buf[LINE_MAX];
    char *Argv[ARG_MAX];
    char *Work[ARG_MAX];
    int Argc = 0;
    int WorkArgc;
    int i;
    int a;
    int Idx;
    int s;
    int Ua;
    const COMMAND_ALIAS *Al = 0;
    const USER_ALIAS *UaPtr = 0;

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

    /* 用户别名优先（可盖住内置别名） */
    Ua = FindUserAliasIndex(Argv[0]);
    if (Ua >= 0) {
        UaPtr = &gUserAliases[Ua];
    } else {
        for (a = 0; a < gAliasCount; a++) {
            if (StrEq(Argv[0], gAliases[a].Alias)) {
                Al = &gAliases[a];
                break;
            }
        }
    }

    WorkArgc = 0;
    if (UaPtr != 0 && UaPtr->Level2[0] != 0) {
        if (Argc + 1 > ARG_MAX) {
            ConsoleWrite("too many args\n");
            return;
        }
        Work[WorkArgc++] = (char *)UaPtr->Level1;
        Work[WorkArgc++] = (char *)UaPtr->Level2;
        for (i = 1; i < Argc; i++) {
            Work[WorkArgc++] = Argv[i];
        }
    } else if (Al != 0 && Al->Level2 != 0) {
        if (Argc + 1 > ARG_MAX) {
            ConsoleWrite("too many args\n");
            return;
        }
        Work[WorkArgc++] = (char *)Al->Level1;
        Work[WorkArgc++] = (char *)Al->Level2;
        for (i = 1; i < Argc; i++) {
            Work[WorkArgc++] = Argv[i];
        }
    } else {
        for (i = 0; i < Argc; i++) {
            Work[i] = Argv[i];
        }
        WorkArgc = Argc;
        if (UaPtr != 0) {
            Work[0] = (char *)UaPtr->Level1;
        } else if (Al != 0) {
            Work[0] = (char *)Al->Level1;
        }
    }

    Idx = FindCommandIndex(Work[0]);
    if (Idx < 0) {
        ConsoleWrite("unknown: ");
        ConsoleWrite(Work[0]);
        ConsoleWrite("  (help)\n");
        return;
    }

    if (gCommands[Idx].SubCount > 0) {
        if (WorkArgc >= 2) {
            for (s = 0; s < gCommands[Idx].SubCount; s++) {
                if (StrEq(Work[1], gCommands[Idx].Subs[s].Name)) {
                    gCommands[Idx].Subs[s].Handler(WorkArgc - 1, &Work[1]);
                    return;
                }
            }
            if (gCommands[Idx].Handler != 0) {
                gCommands[Idx].Handler(WorkArgc, Work);
                return;
            }
            PrintLevel1Usage(&gCommands[Idx]);
            return;
        }
        if (gCommands[Idx].Handler != 0) {
            gCommands[Idx].Handler(WorkArgc, Work);
            return;
        }
        PrintLevel1Usage(&gCommands[Idx]);
        return;
    }

    if (gCommands[Idx].Handler != 0) {
        gCommands[Idx].Handler(WorkArgc, Work);
        return;
    }
    ConsoleWrite("unknown: ");
    ConsoleWrite(Work[0]);
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

/* 注册 help / clear / echo / alias（须在 ShellCommands 之前调用） */
void ConsoleRegisterBuiltins(void) {
    ConsoleRegister("help", "list commands", CommandHelp);
    ConsoleRegisterAlias("help", "?");
    ConsoleRegister("clear", "clear screen", CommandClear);
    ConsoleRegisterAlias("clear", "cls");
    ConsoleRegister("echo", "print arguments", CommandEcho);
    ConsoleRegister("alias", "alias [name[=]cmd [sub]] (user; TOYOS.DB)", CommandAlias);
    ConsoleRegister("unalias", "remove user alias", CommandUnalias);
}

/* 将控制台输出限制在当前焦点窗口客户区内（不重置光标） */
void ConsoleBindFocus(void) {
    GuiFocusApply();
}

/* 初始化：无 Shell 时仅串口提示；开窗后由 ConsoleOnShellOpened 画欢迎语 */
void ConsoleInit(void) {
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

void ConsoleOnWheel(INT8 Wheel) {
    UINT32 Cx;
    UINT32 Cy;
    UINT32 Cw;
    UINT32 Ch;
    UINT32 Bg;
    UINT32 LineH;
    int Vis;
    int MaxOff;
    int Next;

    if (Wheel == 0 || !GuiShellAcceptsInput()) {
        return;
    }
    if (!GuiFocusClient(&Cx, &Cy, &Cw, &Ch, &Bg) || Cw == 0 || Ch == 0) {
        return;
    }
    LineH = FontAdvanceY();
    if (LineH < 8) {
        LineH = 16;
    }
    Vis = (int)(Ch / LineH);
    if (Vis < 1) {
        Vis = 1;
    }
    /* 正滚轮 = 看更早的行 → 增大 gViewOff */
    MaxOff = gSbCount - Vis;
    if (gAccLen > 0 && MaxOff > 0) {
        /* 留一行给当前输入时，历史上限略紧 */
        MaxOff = gSbCount - (Vis - 1);
    }
    if (MaxOff < 0) {
        MaxOff = 0;
    }
    Next = gViewOff + (int)Wheel;
    if (Next < 0) {
        Next = 0;
    }
    if (Next > MaxOff) {
        Next = MaxOff;
    }
    if (Next == gViewOff) {
        return;
    }
    gViewOff = Next;
    ConsoleSbPaint();
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
    RunLine();
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
