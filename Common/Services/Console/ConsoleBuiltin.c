/*
 * ConsoleBuiltin.c — help / clear / echo 与内置注册
 * 核心：ConsoleCmd.c
 */
#include "Console.h"
#include "ConsolePriv.h"
#include "Gui.h"
#include "Hal.h"
#include "Db.h"
#include "ShellCommands.h"

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

void ConsoleRegisterBuiltins(void) {
    ConsoleRegister("help", "list commands", CommandHelp);
    ConsoleRegisterAlias("help", "?");
    ConsoleRegister("clear", "clear screen", CommandClear);
    ConsoleRegisterAlias("clear", "cls");
    ConsoleRegister("echo", "print arguments", CommandEcho);
    ConsoleRegister("alias", "alias [name[=]cmd [sub]] (user; TOYOS.DB)", CommandAlias);
    ConsoleRegister("unalias", "remove user alias", CommandUnalias);
}
