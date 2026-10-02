/*
 * ShellCommandsFsUiStoreJob.c — PR-S3-shellfsui-1：store Job/列表帮手
 */
#include "ShellPrivate.h"
#include "Console.h"
#include "Store.h"
#include "StoreJob.h"
#include "Fat.h"
#include "Hal.h"
#include "HalDevices.h"

int ShellStoreJob(STORE_JOB_KIND Kind, const char *Id) {
    ConsoleJobHoldPrompt();
    if (StoreJobShellRun(Kind, Id) != 0) {
        ConsoleWrite("store: busy (Cancel in Store UI, or store job)\n");
        ConsoleJobReleasePrompt();
        return -1;
    }
    return 0;
}

void ShellStoreQueued(const char *Verb, const char *Id) {
    ConsoleWrite("store: queued ");
    if (Verb && Verb[0]) {
        ConsoleWrite(Verb);
        ConsoleWrite(" ");
    }
    if (Id && Id[0]) {
        ConsoleWrite(Id);
    }
    ConsoleWrite("\n");
    ConsoleWrite("hint: worker runs it; open another shell if needed\n");
}

void ShellStoreNetDone(const char *Verb, int Err) {
    if (Err == 0) {
        ConsoleWrite("store: ");
        ConsoleWrite(Verb);
        ConsoleWrite(" ok\n");
        return;
    }
    ConsoleWrite("store: ");
    ConsoleWrite(Verb);
    ConsoleWrite(" fail\n");
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

void ShellStoreJobStatus(void) {
    char Buf[96];
    int Err;
    int i;

    StoreJobShellPumpBegin();
    for (i = 0; i < 16 && StoreJobUiIsBusy(); i++) {
        if (StoreJobStep() != 0) {
            break;
        }
    }
    StoreJobShellPumpEnd();

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

int StoreWordEq(const char *A, const char *B) {
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

void StorePrintUsage(void) {
    ConsoleWrite(
        "usage: store <list|install|remove|combo|uncombo|installed|sync|fetch|repo|job> ...\n");
    ConsoleWrite(
        "  aliases: (none)→list, status→list, rm→remove, list-installed→installed\n");
}

void StoreCmdListCatalog(void) {
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
    ConsoleWrite(")  *=[本|网] id …\n");
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
        ConsoleWrite(On ? "* " : "  ");
        ConsoleWrite(Tab[i].Origin == STORE_SRC_NET ? "[网] " : "[本] ");
        ConsoleWrite(Tab[i].Id);
        ConsoleWrite("  ");
        ConsoleWrite(Tab[i].Type);
        ConsoleWrite("  dep=");
        ConsoleWrite(Dep);
        ConsoleWrite("  ");
        ConsoleWrite(Tab[i].File);
        ConsoleWrite("  ");
        ConsoleWrite(Tab[i].Title);
        ConsoleWrite("\n");
    }
}
