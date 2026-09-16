/*
 * ConsoleCmd.c — 命令表 / Register / 别名 / help（PR-S-console-split-2）
 *
 * 从 Console.c 迁出；只搬家、不改逻辑。绘制与输入仍在 Console.c。
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
    for (i = 0; i < gCommandCount; i++) {
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
