/*
 * ShellCommandsFsUiStore.c — PR-S3-shellfsui-1：CommandStore + 注册
 */
#include "ShellPrivate.h"
#include "Console.h"
#include "Store.h"
#include "StoreJob.h"
#include "Fat.h"
#include "Hal.h"
#include "LwIp.h"
#include "HalDevices.h"

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

void ShellCommandsFsUiStoreRegister(void) {
    ConsoleRegister("store", "store list|install|remove|combo|uncombo|installed|…", CommandStore);
}
