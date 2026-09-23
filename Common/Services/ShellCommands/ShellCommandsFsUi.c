/*
 * ShellCommandsFsUi.c — PR-S-shell-split-3：shell/settings/files/edit/font/store/lang…
 *
 * 从 ShellCommands.c 原样搬家；不改语义。
 */
#include "ShellPrivate.h"
#include "Console.h"
#include "Gui.h"
#include "Locale.h"
#include "Font.h"
#include "Theme.h"
#include "Store.h"
#include "StoreJob.h"
#include "Desktop.h"
#include "Fat.h"
#include "Hal.h"
#include "LwIp.h"

/*
 * Shell ↔ StoreJob：同一套 Job。0=已入队（Worker 执行）；-1=忙/失败。
 * 完成态：store job；勿在此同步等 LastError（rm-exc-11 INTERFACE）。
 */
static int ShellStoreJob(STORE_JOB_KIND Kind, const char *Id) {
    if (StoreJobShellRun(Kind, Id) != 0) {
        ConsoleWrite("store: busy (Cancel in Store UI, or store job)\n");
        return -1;
    }
    return 0;
}

static void ShellStoreQueued(const char *Verb, const char *Id) {
    ConsoleWrite("store: queued ");
    if (Verb && Verb[0]) {
        ConsoleWrite(Verb);
        ConsoleWrite(" ");
    }
    if (Id && Id[0]) {
        ConsoleWrite(Id);
    }
    ConsoleWrite("\n");
    ConsoleWrite("hint: worker runs it; store job — status / last error\n");
}

/*
 * lwip on 时 HTTP 须与 Shell 同核泵栈；Enqueue→Worker 易 busy 挂死。
 * Shell 同步跑 fetch/sync，输出自然在下一行 toyos> 之前。
 */
static void ShellStoreNetDone(const char *Verb, int Err) {
    if (Err == 0) {
        ConsoleWrite("store: ");
        ConsoleWrite(Verb);
        ConsoleWrite(" ok\n");
        if (Verb[0] == 'f') {
            ConsoleWrite("hint: store install <id>\n");
        }
        return;
    }
    ConsoleWrite("store: ");
    ConsoleWrite(Verb);
    ConsoleWrite(" fail\n");
    /* STORE_ERR_*：net=-40 http=-41 hash=-42 alloc=-43 */
    if (Err == -41) {
        ConsoleWrite("hint: HTTP not 200\n");
    } else if (Err == -42) {
        ConsoleWrite("hint: hash mismatch\n");
    } else if (Err == -40) {
        ConsoleWrite("hint: net/tcp fail\n");
    } else if (Err == -43) {
        ConsoleWrite("hint: out of memory\n");
    }
}

static void ShellStoreJobStatus(void) {
    char Buf[96];
    int Err;

    if (StoreJobUiIsBusy()) {
        StoreJobGetStatus(Buf, (int)sizeof(Buf));
        ConsoleWrite("store job: busy");
        if (Buf[0]) {
            ConsoleWrite("  ");
            ConsoleWrite(Buf);
        }
        ConsoleWrite("\n");
        return;
    }
    Err = StoreJobLastError();
    ConsoleWrite("store job: idle  last=");
    if (Err == FAT_OK || Err == 0) {
        ConsoleWrite("ok");
    } else if (Err == STORE_JOB_ERR_CANCEL) {
        ConsoleWrite("cancelled");
    } else {
        ConsoleWrite(FatStrError(Err));
    }
    ConsoleWrite("\n");
}

static void CommandShell(int Argc, char **Argv) {
    int Idx;

    (void)Argc;
    (void)Argv;
    Idx = GuiOpenShell();
    if (Idx < 0) {
        ConsoleWrite("shell: no free window\n");
        return;
    }
    /* 欢迎语已在 GuiOpenShell 淡入前画好 */
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
        "usage: store <list|install|remove|combo|uncombo|installed|sync|fetch|repo|job> ...\n");
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
        } else if (StoreWordEq(Sub, "job")) {
            ShellStoreJobStatus();
            return;
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
        /* lwIP：Shell 同步泵；否则仍可走 Worker Job */
        if (LwIpActive()) {
            int Err;

            if (StoreJobShellBegin() != 0) {
                ConsoleWrite("store: busy (Cancel in Store UI, or store job)\n");
                return;
            }
            ConsoleWrite("store: syncing catalog...\n");
            Err = StoreSyncCatalog();
            StoreJobShellEnd();
            ShellStoreNetDone("sync", Err);
            return;
        }
        if (ShellStoreJob(STORE_JOB_SYNC, 0) != 0) {
            return;
        }
        ShellStoreQueued("sync", 0);
        return;
    }

    if (StoreWordEq(Sub, "fetch")) {
        if (Argc < 3) {
            ConsoleWrite("usage: store fetch <id>\n");
            return;
        }
        if (LwIpActive()) {
            int Err;

            if (StoreJobShellBegin() != 0) {
                ConsoleWrite("store: busy (Cancel in Store UI, or store job)\n");
                return;
            }
            ConsoleWrite("store: fetching ");
            ConsoleWrite(Argv[2]);
            ConsoleWrite("...\n");
            Err = StoreFetchId(Argv[2]);
            StoreJobShellEnd();
            ShellStoreNetDone("fetch", Err);
            return;
        }
        if (ShellStoreJob(STORE_JOB_FETCH, Argv[2]) != 0) {
            return;
        }
        ShellStoreQueued("fetch", Argv[2]);
        return;
    }

    if (StoreWordEq(Sub, "install") || StoreWordEq(Sub, "combo")) {
        if (Argc < 3) {
            if (StoreWordEq(Sub, "combo")) {
                ConsoleWrite("usage: store combo <id>\n");
                ConsoleWrite("hint: e.g. store combo guidemo  (demopack+sun8 then app)\n");
            } else {
                ConsoleWrite("usage: store install <id>\n");
            }
            return;
        }
        /* 与 Store 窗 Install 同 Job；只入队，Worker 执行（rm-exc-11） */
        if (ShellStoreJob(STORE_JOB_INSTALL, Argv[2]) != 0) {
            return;
        }
        ShellStoreQueued(StoreWordEq(Sub, "combo") ? "combo" : "install", Argv[2]);
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

    if (StoreWordEq(Sub, "remove") || StoreWordEq(Sub, "uncombo")) {
        if (Argc < 3) {
            ConsoleWrite(StoreWordEq(Sub, "uncombo")
                             ? "usage: store uncombo <id>\n"
                             : "usage: store remove <id>\n");
            return;
        }
        /* 与 Store 窗 Remove 同 Job；只入队（rm-exc-11 INTERFACE） */
        if (ShellStoreJob(STORE_JOB_REMOVE, Argv[2]) != 0) {
            return;
        }
        ShellStoreQueued(StoreWordEq(Sub, "uncombo") ? "uncombo" : "remove", Argv[2]);
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


void ShellCommandsFsUiRegister(void) {
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
