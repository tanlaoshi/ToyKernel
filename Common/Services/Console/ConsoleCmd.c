/*
 * ConsoleCmd.c — 命令表 / Register / 分发
 * 核心文件。别名：ConsoleAlias.c；内置：ConsoleBuiltin.c。
 */
#include "Console.h"
#include "ConsolePriv.h"
#include "Gui.h"
#include "Hal.h"
#include "Db.h"
#include "ShellCommands.h"

COMMAND gCommands[CMD_MAX];
int gCommandCount;
COMMAND_ALIAS gAliases[ALIAS_MAX];
int gAliasCount;
USER_ALIAS gUserAliases[USER_ALIAS_MAX];
int gUserAliasCount;

int StrEq(const char *A, const char *B) {
    while (*A && *B) {
        if (*A != *B) {
            return 0;
        }
        A++;
        B++;
    }
    return *A == *B;
}
int FindCommandIndex(const char *Name) {
    int i;

    for (i = 0; i < gCommandCount; i++) {
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
    if (gCommandCount >= CMD_MAX) {
        HalConsoleWriteSerial("console: CMD_MAX full, drop ");
        HalConsoleWriteSerial(Name);
        HalConsoleWriteSerial("\n");
        return;
    }
    gCommands[gCommandCount].Name = Name;
    gCommands[gCommandCount].Help = Help ? Help : "";
    gCommands[gCommandCount].Handler = Handler;
    gCommands[gCommandCount].SubCount = 0;
    gCommandCount++;
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
        if (gCommandCount >= CMD_MAX) {
            HalConsoleWriteSerial("console: CMD_MAX full, drop ");
            HalConsoleWriteSerial(Level1);
            HalConsoleWriteSerial("\n");
            return;
        }
        Idx = gCommandCount;
        gCommands[Idx].Name = Level1;
        gCommands[Idx].Help = "";
        gCommands[Idx].Handler = 0;
        gCommands[Idx].SubCount = 0;
        gCommandCount++;
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
void ConsoleRunLine(void) {
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
