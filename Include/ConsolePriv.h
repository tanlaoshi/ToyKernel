/*
 * ConsolePriv.h — Console / ConsoleScroll / ConsoleCmd 内部
 * （仅 Common/Services；User 勿 include）
 *
 * PR-S-console-split-1：Scroll；PR-S-console-split-2：Cmd（Register/别名/help）。
 */
#ifndef CONSOLE_PRIV_H
#define CONSOLE_PRIV_H

#include "BootTypes.h"

#define SB_LINES 64
#define SB_COLS  120

#define LINE_MAX 128
#define ARG_MAX  8
/* builtins+Shell+FS+Db+lwip 已超 32；满表时 ConsoleRegister 静默失败会丢末尾命令（如 lwip） */
#define CMD_MAX  48
#define SUB_MAX  12
#define ALIAS_MAX 80
#define USER_ALIAS_MAX 16
#define USER_ALIAS_NAME 20
#define USER_ALIAS_WORD 16

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

/* 输入行态（Console.c）；Cmd 清屏/分发可读改 */
extern char gLine[LINE_MAX];
extern int gLen;
extern int gWaitPrompt;
extern int gPromptSuspend;
extern int gAtLineStart;

void ConsoleDrawString(const char *Text, UINT32 Color);
void ConsoleDrawChar(char C, UINT32 Color);
void ConsoleRunLine(void);

void ConsoleSbReset(void);
void ConsoleSbFeed(const char *Text);
void ConsoleSbFeedChar(char C);
void ConsoleSbBackspace(void);
void ConsoleSbPaint(void);
void ConsoleSbEnsureLive(void);

#endif
