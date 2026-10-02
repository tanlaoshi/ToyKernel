/*
 * ConsoleAlias.c — 用户别名与 alias / unalias
 * 核心：ConsoleCmd.c
 */
#include "Console.h"
#include "ConsolePrivate.h"
#include "Gui.h"
#include "Hal.h"
#include "Db.h"
#include "ShellCommands.h"

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

int FindUserAliasIndex(const char *Name) {
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
void CommandAlias(int Argc, char **Argv) {
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

void CommandUnalias(int Argc, char **Argv) {
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
