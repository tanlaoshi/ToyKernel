/*
 * ShellCmdFsUi.c — PR-S-shell-split-3：shell/settings/files/edit/font/store/lang…
 *
 * 从 ShellCommands.c 原样搬家；不改语义。
 */
#include "ShellPriv.h"
#include "Console.h"
#include "Gui.h"
#include "Locale.h"
#include "Font.h"
#include "Theme.h"
#include "Store.h"
#include "Fat.h"
#include "Hal.h"

static void CommandShell(int Argc, char **Argv) {
    int Idx;

    (void)Argc;
    (void)Argv;
    Idx = GuiOpenShell();
    if (Idx < 0) {
        ConsoleWrite("shell: no free window\n");
        return;
    }
    ConsoleOnShellOpened();
}

static void CommandSettings(int Argc, char **Argv) {
    int Idx;

    (void)Argc;
    (void)Argv;
    Idx = GuiOpenSettings();
    if (Idx < 0) {
        ConsoleWrite("settings: no free window\n");
    }
}

static void CommandFiles(int Argc, char **Argv) {
    int Idx;

    (void)Argc;
    (void)Argv;
    Idx = GuiOpenFiles();
    if (Idx < 0) {
        ConsoleWrite("files: no free window\n");
    }
}

static void CommandEdit(int Argc, char **Argv) {
    int Idx;
    const char *Path;

    if (Argc < 2 || !Argv[1] || !Argv[1][0]) {
        ConsoleWrite("usage: edit <path>\n");
        return;
    }
    Path = Argv[1];
    Idx = GuiOpenEdit(Path);
    if (Idx < 0) {
        ConsoleWrite("edit: no free window\n");
    }
}

static void CommandZh(int Argc, char **Argv) {
    (void)Argc;
    (void)Argv;
    /* PR-I18N1：UTF-8 + CJK16 子集冒烟 */
    ConsoleWrite("你好，世界！中文测试\n");
}

static int StoreWordEq(const char *A, const char *B) {
    if (A == 0 || B == 0) {
        return 0;
    }
    while (*A && *B) {
        if (*A != *B) {
            return 0;
        }
        A++;
        B++;
    }
    return *A == *B;
}

static void StorePrintUsage(void) {
    ConsoleWrite(
        "usage: store <list|install|remove|combo|uncombo|installed|sync|fetch|repo> ...\n");
    ConsoleWrite(
        "  aliases: (none)→list, status→list, rm→remove, list-installed→installed\n");
}

static void StoreCmdListCatalog(void) {
    STORE_ENTRY *Tab;
    int Count = 0;
    int i;
    int Err;

    Tab = StoreScratchTab();
    Err = StoreLoadCatalog(Tab, STORE_ENTRIES_MAX, &Count);
    if (Err < 0) {
        ConsoleWrite("store: catalog ");
        ConsoleWrite(FatStrError(Err));
        ConsoleWrite("\n");
        return;
    }
    ConsoleWrite("store (");
    ConsoleWrite(StoreHostArch());
    ConsoleWrite(")  state=INST|avail  dep=...\n");
    for (i = 0; i < Count; i++) {
        char Dep[64];
        int On = StoreIsInstalled(Tab[i].Id);
        if (On) {
            (void)StoreGetDepends(Tab[i].Id, Dep, (int)sizeof(Dep));
        } else if (Tab[i].Depends[0]) {
            int k = 0;
            while (Tab[i].Depends[k] && k < (int)sizeof(Dep) - 1) {
                Dep[k] = Tab[i].Depends[k];
                k++;
            }
            Dep[k] = 0;
        } else {
            Dep[0] = '-';
            Dep[1] = 0;
        }
        ConsoleWrite("  ");
        ConsoleWrite(Tab[i].Id);
        ConsoleWrite("  ");
        ConsoleWrite(Tab[i].Type);
        ConsoleWrite(On ? "  INST  " : "  avail ");
        ConsoleWrite("dep=");
        ConsoleWrite(Dep);
        ConsoleWrite("  ");
        ConsoleWrite(Tab[i].File);
        ConsoleWrite("  ");
        ConsoleWrite(Tab[i].Title);
        ConsoleWrite("\n");
    }
}

static void CommandStore(int Argc, char **Argv) {
    const char *Sub;
    int i;
    int Err;
    UINT32 Ip;
    UINT16 Port;
    char IpBuf[24];

    /* PR-C3：正统二级无中横线；别名在此展开 */
    if (Argc < 2) {
        Sub = "list";
    } else {
        Sub = Argv[1];
        if (StoreWordEq(Sub, "status")) {
            Sub = "list";
        } else if (StoreWordEq(Sub, "rm")) {
            Sub = "remove";
        } else if (StoreWordEq(Sub, "list-installed")) {
            Sub = "installed";
        }
    }

    if (StoreWordEq(Sub, "repo")) {
        if (Argc >= 3) {
            if (StoreRepoSet(Argv[2]) != 0) {
                ConsoleWrite("store repo: bad ip:port\n");
                return;
            }
            ConsoleWrite("store: repo set\n");
            return;
        }
        StoreRepoGet(&Ip, &Port);
        HalNetFormatIp(Ip, IpBuf, (int)sizeof(IpBuf));
        ConsoleWrite("store repo ");
        ConsoleWrite(IpBuf);
        ConsoleWrite(":");
        ConsoleWriteHex32(Port);
        ConsoleWrite("\n");
        return;
    }

    if (StoreWordEq(Sub, "sync")) {
        Err = StoreSyncCatalog();
        if (Err == -41 || Err == -2) {
            ConsoleWrite("store sync: HTTP not 200 (host http.server + /catalog.txt?)\n");
            return;
        }
        if (Err == -40) {
            ConsoleWrite("store sync: net/tcp fail (repo up? store repo)\n");
            return;
        }
        if (Err == -43) {
            ConsoleWrite("store sync: out of memory\n");
            return;
        }
        if (Err != 0) {
            ConsoleWrite("store sync: ");
            ConsoleWrite(FatStrError(Err));
            ConsoleWrite("\n");
            return;
        }
        ConsoleWrite("store: synced Store/catalog.txt\n");
        return;
    }

    if (StoreWordEq(Sub, "fetch")) {
        if (Argc < 3) {
            ConsoleWrite("usage: store fetch <id>\n");
            return;
        }
        Err = StoreFetchId(Argv[2]);
        if (Err == -41 || Err == -2) {
            ConsoleWrite("store fetch: HTTP not 200\n");
            return;
        }
        if (Err == -42 || Err == -3) {
            ConsoleWrite("store fetch: hash mismatch\n");
            return;
        }
        if (Err == -40) {
            ConsoleWrite("store fetch: net/tcp fail\n");
            return;
        }
        if (Err == -43) {
            ConsoleWrite("store fetch: out of memory\n");
            return;
        }
        if (Err != 0) {
            ConsoleWrite("store fetch: ");
            ConsoleWrite(FatStrError(Err));
            ConsoleWrite("\n");
            return;
        }
        ConsoleWrite("store: fetched to Store/\n");
        ConsoleWrite("hint: store install <id>\n");
        return;
    }

    if (StoreWordEq(Sub, "install")) {
        if (Argc < 3) {
            ConsoleWrite("usage: store install <id>\n");
            return;
        }
        Err = StoreInstall(Argv[2]);
        if (Err != FAT_OK) {
            ConsoleWrite("store install: ");
            ConsoleWrite(FatStrError(Err));
            ConsoleWrite("\n");
            ConsoleWrite("hint: store combo <id> installs depends first\n");
            return;
        }
        {
            STORE_ENTRY *Tab2 = StoreScratchTab();
            int C2 = 0;
            int j;
            const char *Where = "Apps/";
            (void)StoreLoadCatalog(Tab2, STORE_ENTRIES_MAX, &C2);
            for (j = 0; j < C2; j++) {
                int k = 0;
                while (Argv[2][k] && Argv[2][k] == Tab2[j].Id[k]) {
                    k++;
                }
                if (Argv[2][k] == 0 && Tab2[j].Id[k] == 0) {
                    if (Tab2[j].Type[0] == 'f') {
                        Where = "Assets/Fonts/";
                    } else if (Tab2[j].Type[0] == 'a' && Tab2[j].Type[1] == 's') {
                        Where = "Assets/Packs/";
                    }
                    break;
                }
            }
            ConsoleWrite("store: installed to ");
            ConsoleWrite(Where);
            ConsoleWrite("\n");
            if (Where[0] == 'A' && Where[7] == 'F') {
                ConsoleWrite("hint: font / font reload — Settings 可选新字面\n");
            } else if (Where[0] == 'A' && Where[7] == 'P') {
                ConsoleWrite("hint: blob under Assets/Packs/ (driver reads path)\n");
            } else {
                ConsoleWrite("hint: exec Apps/<ELF> — HELLO prints one line then exits (正常)\n");
            }
        }
        return;
    }

    if (StoreWordEq(Sub, "installed")) {
        STORE_INSTALLED Inst[STORE_INSTALLED_MAX];
        int N = 0;
        Err = StoreListInstalled(Inst, STORE_INSTALLED_MAX, &N);
        if (Err != FAT_OK) {
            ConsoleWrite("store installed: ");
            ConsoleWrite(FatStrError(Err));
            ConsoleWrite("\n");
            return;
        }
        ConsoleWrite("store installed:\n");
        if (N == 0) {
            ConsoleWrite("  (none)\n");
            return;
        }
        for (i = 0; i < N; i++) {
            char Dep[64];
            ConsoleWrite("  ");
            ConsoleWrite(Inst[i].Id);
            ConsoleWrite("  ");
            ConsoleWrite(Inst[i].Type);
            ConsoleWrite("  ");
            ConsoleWrite(Inst[i].File);
            ConsoleWrite("  dep=");
            (void)StoreGetDepends(Inst[i].Id, Dep, (int)sizeof(Dep));
            ConsoleWrite(Dep);
            ConsoleWrite("\n");
        }
        return;
    }

    if (StoreWordEq(Sub, "remove")) {
        if (Argc < 3) {
            ConsoleWrite("usage: store remove <id>\n");
            return;
        }
        Err = StoreRemove(Argv[2]);
        if (Err != FAT_OK) {
            ConsoleWrite("store remove: ");
            ConsoleWrite(FatStrError(Err));
            ConsoleWrite("\n");
            ConsoleWrite("hint: still required? store uncombo <leaf>\n");
            return;
        }
        ConsoleWrite("store: removed ");
        ConsoleWrite(Argv[2]);
        ConsoleWrite("\n");
        return;
    }

    if (StoreWordEq(Sub, "combo")) {
        if (Argc < 3) {
            ConsoleWrite("usage: store combo <id>\n");
            ConsoleWrite("hint: e.g. store combo guidemo  (demopack+sun8 then app)\n");
            return;
        }
        Err = StoreComboInstall(Argv[2]);
        if (Err != FAT_OK) {
            ConsoleWrite("store combo: ");
            ConsoleWrite(FatStrError(Err));
            ConsoleWrite("\n");
            return;
        }
        ConsoleWrite("store: combo installed ");
        ConsoleWrite(Argv[2]);
        ConsoleWrite(" (+depends)\n");
        return;
    }

    if (StoreWordEq(Sub, "uncombo")) {
        if (Argc < 3) {
            ConsoleWrite("usage: store uncombo <id>\n");
            return;
        }
        Err = StoreComboRemove(Argv[2]);
        if (Err != FAT_OK) {
            ConsoleWrite("store uncombo: ");
            ConsoleWrite(FatStrError(Err));
            ConsoleWrite("\n");
            return;
        }
        ConsoleWrite("store: combo removed ");
        ConsoleWrite(Argv[2]);
        ConsoleWrite("\n");
        return;
    }

    if (StoreWordEq(Sub, "list")) {
        StoreCmdListCatalog();
        return;
    }

    StorePrintUsage();
}

static void CommandFont(int Argc, char **Argv) {
    UINT32 i;
    UINT32 Id;
    const FONT_FACE *F;

    if (Argc >= 2 && Argv[1][0] == 'r' && Argv[1][1] == 'e' &&
        Argv[1][2] == 'l' && Argv[1][3] == 'o' && Argv[1][4] == 'a' &&
        Argv[1][5] == 'd' && Argv[1][6] == 0) {
        (void)FontReloadAssets();
        ThemeClampFontId();
        ConsoleWrite("Font: assets reloaded\n");
        return;
    }
    if (Argc >= 2 && Argv[1][0] >= '0' && Argv[1][0] <= '9') {
        Id = 0;
        for (i = 0; Argv[1][i] >= '0' && Argv[1][i] <= '9'; i++) {
            Id = Id * 10u + (UINT32)(Argv[1][i] - '0');
        }
        if (Argv[1][i] != 0 || ThemeSetFontId(Id) != 0) {
            ConsoleWrite("Font: bad id\n");
            return;
        }
        ThemeApply();
        ConsoleWrite("Font: set ");
        F = FontGetCurrent();
        ConsoleWrite(F && F->Name ? F->Name : "?");
        ConsoleWrite("\n");
        return;
    }
    ConsoleWrite("fonts:\n");
    for (i = 0; i < FontCount(); i++) {
        F = FontGetById(i);
        ConsoleWrite(i == FontCurrentId() ? " * " : "   ");
        ConsoleWrite(F && F->Name ? F->Name : "?");
        ConsoleWrite("\n");
    }
    if (Argc < 2) {
        ConsoleWrite("usage: font [reload|<id>]\n");
    }
}

static void CommandLang(int Argc, char **Argv) {
    if (Argc < 2) {
        ConsoleWrite(LocStr(MSG_LANG_USAGE));
        ConsoleWrite(LocStr(MSG_LANG_NOW));
        ConsoleWrite(LocaleGet() == LOC_LANG_ZH ? "zh\n" : "en\n");
        return;
    }
    if (Argv[1][0] == 'r' && Argv[1][1] == 'e' && Argv[1][2] == 'l' &&
        Argv[1][3] == 'o' && Argv[1][4] == 'a' && Argv[1][5] == 'd' &&
        Argv[1][6] == 0) {
        /* lang reload — 重读 Assets/Locale/en.txt 与 zh.txt */
        LocaleReload();
        ConsoleWrite("locale reloaded\n");
        return;
    }
    if (Argv[1][0] == 'z' && Argv[1][1] == 'h') {
        (void)LocaleSet(LOC_LANG_ZH);
        ConsoleWrite(LocStr(MSG_LANG_SET));
        return;
    }
    if (Argv[1][0] == 'e' && Argv[1][1] == 'n') {
        (void)LocaleSet(LOC_LANG_EN);
        ConsoleWrite(LocStr(MSG_LANG_SET));
        return;
    }
    ConsoleWrite(LocStr(MSG_LANG_BAD));
}


void ShellCmdFsUiRegister(void) {
    ConsoleRegister2("test", "glyph", "UTF-8 Chinese glyph test", CommandZh);
    ConsoleRegisterAliasLine("zh", "test", "glyph");

    ConsoleRegister2("set", "language", "set language en|zh|reload", CommandLang);
    ConsoleRegisterAliasLine("lang", "set", "language");
    ConsoleRegisterAliasLine("language", "set", "language");

    ConsoleRegister("shell", "open Shell window", CommandShell);
    ConsoleRegister("settings", "open Settings window", CommandSettings);
    ConsoleRegister("files", "open Files browser", CommandFiles);
    ConsoleRegister("edit", "edit <path> open text editor (PR-V2)", CommandEdit);
    ConsoleRegister("font", "font [reload|<id>] (Assets/Fonts TOYF)", CommandFont);
    ConsoleRegister("store", "store list|install|remove|combo|uncombo|installed|…", CommandStore);
}
