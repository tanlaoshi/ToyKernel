/*
 * StoreJob.c — Store UI 作业状态机（PR-S-job）
 * 序 3：Step 每相前进（Plan / 每包 / Font / Reload / Finish）。
 */
#include "StoreUiPrivate.h"
#include "StoreJob.h"
#include "Fat.h"
#include "HalConsole.h"

typedef enum {
    JP_IDLE = 0,
    JP_PLAN,
    JP_PKG,
    JP_FONT,
    JP_RELOAD,
    JP_FINISH
} STORE_JOB_PHASE;

static STORE_JOB_KIND sPendingKind;
static STORE_JOB_KIND sKind;
static STORE_JOB_PHASE sPhase;
static int sRunning;
static int sInStep;
static char sJobId[STORE_ID_MAX];
static char sPlan[STORE_ENTRIES_MAX][STORE_ID_MAX];
static int sPlanN;
static int sPlanI;
static int sErr;
static int sBatch;

static void StatusWithId(const char *Verb, const char *Id) {
    char Buf[80];
    int i = 0;
    int j;

    if (!Verb) {
        Verb = "?";
    }
    while (Verb[i] && i < 20) {
        Buf[i] = Verb[i];
        i++;
    }
    if (Id && Id[0] && i < 76) {
        Buf[i++] = ':';
        Buf[i++] = ' ';
        for (j = 0; Id[j] && i < 78; j++) {
            Buf[i++] = Id[j];
        }
    }
    Buf[i] = 0;
    StoreSetStatus(Buf);
}

static void StatusProgress(const char *Verb, const char *Id, int Cur, int Total) {
    char Buf[80];
    int i = 0;
    int j;
    int n;

    if (!Verb) {
        Verb = "?";
    }
    while (Verb[i] && i < 16) {
        Buf[i] = Verb[i];
        i++;
    }
    if (Id && Id[0] && i < 60) {
        Buf[i++] = ':';
        Buf[i++] = ' ';
        for (j = 0; Id[j] && i < 60; j++) {
            Buf[i++] = Id[j];
        }
    }
    if (Total > 0 && i < 70) {
        Buf[i++] = ' ';
        Buf[i++] = '(';
        n = Cur;
        if (n >= 10 && i < 76) {
            Buf[i++] = (char)('0' + (n / 10) % 10);
        }
        if (i < 76) {
            Buf[i++] = (char)('0' + n % 10);
        }
        Buf[i++] = '/';
        n = Total;
        if (n >= 10 && i < 76) {
            Buf[i++] = (char)('0' + (n / 10) % 10);
        }
        if (i < 76) {
            Buf[i++] = (char)('0' + n % 10);
        }
        Buf[i++] = ')';
    }
    Buf[i] = 0;
    StoreSetStatus(Buf);
}

static void BusyRepaint(void) {
    if (StoreUiIsFocused()) {
        StoreUiRepaint();
    }
}

static void FinishStatus(void) {
    if (sErr == FAT_OK && sKind == STORE_JOB_INSTALL && sPlanN == 0) {
        StoreSetStatus("already installed");
        return;
    }
    if (sErr == FAT_OK) {
        if (sKind == STORE_JOB_INSTALL) {
            StoreSetStatus("installed");
        } else if (sKind == STORE_JOB_REMOVE) {
            StoreSetStatus("removed");
        } else {
            StoreSetStatus("sync ok");
        }
    } else if (sKind == STORE_JOB_INSTALL) {
        StoreSetStatus("install fail");
    } else if (sKind == STORE_JOB_REMOVE) {
        StoreSetStatus("remove fail");
    } else {
        StoreSetStatus("sync fail (need repo)");
    }
}

int StoreJobEnqueue(STORE_JOB_KIND Kind, const char *Id) {
    int i;

    if (Kind == STORE_JOB_NONE) {
        return -1;
    }
    if (sRunning || sPendingKind != STORE_JOB_NONE) {
        return -1;
    }
    if (Kind == STORE_JOB_SYNC) {
        sJobId[0] = 0;
    } else {
        if (!Id || !Id[0]) {
            return -1;
        }
        for (i = 0; Id[i] && i < STORE_ID_MAX - 1; i++) {
            sJobId[i] = Id[i];
        }
        sJobId[i] = 0;
    }
    sPendingKind = Kind;
    return 0;
}

int StoreJobStep(void) {
    int Err;

    if (sInStep) {
        return sRunning ? 0 : 1;
    }
    sInStep = 1;

    if (!sRunning) {
        if (sPendingKind == STORE_JOB_NONE) {
            sInStep = 0;
            return 1;
        }
        sKind = sPendingKind;
        sPendingKind = STORE_JOB_NONE;
        sRunning = 1;
        sPhase = JP_PLAN;
        sErr = FAT_OK;
        sPlanN = 0;
        sPlanI = 0;
        sBatch = 0;
    }

    switch (sPhase) {
    case JP_PLAN:
        if (sKind == STORE_JOB_SYNC) {
            StoreSetStatus("sync: catalog");
            BusyRepaint();
            sPhase = JP_PKG;
            sInStep = 0;
            return 0;
        }
        StoreComboBatchBegin();
        sBatch = 1;
        StatusWithId("plan", sJobId);
        BusyRepaint();
        if (sKind == STORE_JOB_INSTALL) {
            Err = StoreComboPlanInstall(sJobId, sPlan, STORE_ENTRIES_MAX, &sPlanN);
        } else {
            Err = StoreComboPlanRemove(sJobId, sPlan, STORE_ENTRIES_MAX, &sPlanN);
        }
        if (Err != FAT_OK) {
            sErr = Err;
            sPhase = JP_FONT;
            sInStep = 0;
            return 0;
        }
        sPlanI = 0;
        if (sPlanN > 0) {
            sPhase = JP_PKG;
        } else if (sKind == STORE_JOB_INSTALL) {
            /* 已装（或无需拷贝）：提示后走 Reload，不跑装包相 */
            StoreSetStatus("already installed");
            BusyRepaint();
            sPhase = JP_FONT;
        } else {
            sPhase = JP_FONT;
        }
        sInStep = 0;
        return 0;

    case JP_PKG:
        if (sKind == STORE_JOB_SYNC) {
            Err = StoreSyncCatalog();
            if (Err != 0) {
                sErr = Err;
            }
            sPhase = JP_RELOAD;
            sInStep = 0;
            return 0;
        }
        if (sPlanI >= sPlanN) {
            sPhase = JP_FONT;
            sInStep = 0;
            return 0;
        }
        if (!sPlan[sPlanI][0]) {
            sPlanI++;
            if (sPlanI >= sPlanN) {
                sPhase = JP_FONT;
            }
            sInStep = 0;
            return 0;
        }
        StatusProgress(sKind == STORE_JOB_INSTALL ? "install" : "remove",
                       sPlan[sPlanI], sPlanI + 1, sPlanN);
        /* 先做 IO，再 Repaint，避免 Present 路径重入 Step / 搅 vvfat */
        if (sKind == STORE_JOB_INSTALL) {
            HalConsoleWriteSerial("store combo: +");
            HalConsoleWriteSerial(sPlan[sPlanI]);
            HalConsoleWriteSerial("\n");
            Err = StoreInstall(sPlan[sPlanI]);
        } else {
            HalConsoleWriteSerial("store uncombo: -");
            HalConsoleWriteSerial(sPlan[sPlanI]);
            HalConsoleWriteSerial("\n");
            Err = StoreRemove(sPlan[sPlanI]);
            if (sPlanI > 0 && (Err == FAT_ERR_INVAL || Err == FAT_ERR_NOENT)) {
                Err = FAT_OK;
            }
        }
        sPlanI++;
        if (Err != FAT_OK) {
            sErr = Err;
            sPhase = JP_FONT;
        } else if (sPlanI >= sPlanN) {
            sPhase = JP_FONT;
        }
        BusyRepaint();
        sInStep = 0;
        return 0;

    case JP_FONT:
        StoreSetStatus("font/theme...");
        if (sBatch) {
            StoreComboBatchEnd();
            sBatch = 0;
        }
        BusyRepaint();
        sPhase = JP_RELOAD;
        sInStep = 0;
        return 0;

    case JP_RELOAD:
        StoreSetStatus("reload...");
        GuiPollMouseMotion();
        Reload();
        GuiPollMouseMotion();
        DesktopNotifyAppsChanged();
        BusyRepaint();
        sPhase = JP_FINISH;
        sInStep = 0;
        return 0;

    case JP_FINISH:
        FinishStatus();
        BusyRepaint();
        sRunning = 0;
        sPhase = JP_IDLE;
        sKind = STORE_JOB_NONE;
        sInStep = 0;
        return 1;

    default:
        sRunning = 0;
        sPhase = JP_IDLE;
        sInStep = 0;
        return 1;
    }
}

int StoreJobIsBusy(void) {
    return (sRunning || sPendingKind != STORE_JOB_NONE) ? 1 : 0;
}

int StoreJobIsRunning(void) {
    return sRunning ? 1 : 0;
}

void StoreJobGetStatus(char *Out, int OutMax) {
    int i;

    if (!Out || OutMax <= 0) {
        return;
    }
    for (i = 0; gStoreUiStatus[i] && i < OutMax - 1; i++) {
        Out[i] = gStoreUiStatus[i];
    }
    Out[i] = 0;
}

int StoreJobCancel(void) {
    return -1;
}
