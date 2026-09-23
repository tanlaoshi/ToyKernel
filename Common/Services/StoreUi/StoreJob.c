/*
 * StoreJob.c — Store UI 作业状态机（PR-S-job）
 * 序 5：Cancel 在相位/chunk 边界生效。
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
static int sCancel;
static char sJobId[STORE_ID_MAX];
static char sPlan[STORE_ENTRIES_MAX][STORE_ID_MAX];
static int sPlanN;
static int sPlanI;
static int sErr;
static int sLastErr;
static int sBatch;

static void JobApplyCancel(void) {
    StoreInstallPumpAbort();
    if (sBatch) {
        StoreComboBatchEnd();
        sBatch = 0;
    }
    sErr = STORE_JOB_ERR_CANCEL;
    sCancel = 0;
    sPhase = JP_RELOAD;
}

int StoreJobEnqueue(STORE_JOB_KIND Kind, const char *Id) {
    int i;

    if (Kind == STORE_JOB_NONE) {
        return -1;
    }
    if (StoreJobUiIsBusy() || StoreJobShellIsBusy()) {
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
    sCancel = 0;
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
        if (sCancel) {
            sPendingKind = STORE_JOB_NONE;
            sCancel = 0;
            StoreSetStatus("cancelled");
            StoreJobBusyRepaint();
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

    if (sCancel && sRunning) {
        JobApplyCancel();
    }

    switch (sPhase) {
    case JP_PLAN:
        if (sKind == STORE_JOB_SYNC) {
            StoreSetStatus("sync: catalog");
            StoreJobBusyRepaint();
            sPhase = JP_PKG;
            sInStep = 0;
            return 0;
        }
        if (sKind == STORE_JOB_FETCH) {
            StoreJobStatusWithId("fetch", sJobId);
            StoreJobBusyRepaint();
            sPhase = JP_PKG;
            sInStep = 0;
            return 0;
        }
        StoreComboBatchBegin();
        sBatch = 1;
        StoreJobStatusWithId("plan", sJobId);
        StoreJobBusyRepaint();
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
            StoreSetStatus("already installed");
            StoreJobBusyRepaint();
            sPhase = JP_FONT;
        } else {
            sPhase = JP_FONT;
        }
        sInStep = 0;
        return 0;

    case JP_PKG:
        if (sCancel) {
            JobApplyCancel();
            sInStep = 0;
            return 0;
        }
        if (sKind == STORE_JOB_SYNC) {
            Err = StoreSyncCatalog();
            if (Err != 0) {
                sErr = Err;
            }
            sPhase = JP_RELOAD;
            sInStep = 0;
            return 0;
        }
        if (sKind == STORE_JOB_FETCH) {
            Err = StoreFetchId(sJobId);
            if (Err != 0) {
                sErr = Err;
            }
            /* 仅写缓存，不必 font/reload */
            sPhase = JP_FINISH;
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
        StoreJobStatusProgress(sKind == STORE_JOB_INSTALL ? "install" : "remove",
                               sPlan[sPlanI], sPlanI + 1, sPlanN);
        if (sKind == STORE_JOB_INSTALL) {
            if (!StoreInstallPumpBusy()) {
                HalConsoleWriteSerial("store combo: +");
                HalConsoleWriteSerial(sPlan[sPlanI]);
                HalConsoleWriteSerial("\n");
                Err = StoreInstallPump(sPlan[sPlanI]);
            } else {
                Err = StoreInstallPump(0);
            }
            if (Err == 1) {
                UINTN Got = 0;
                UINTN Sz = 0;

                if (sCancel) {
                    JobApplyCancel();
                    sInStep = 0;
                    return 0;
                }
                StoreInstallPumpProgress(&Got, &Sz);
                StoreJobStatusCopy(sPlan[sPlanI], Got, Sz);
                StoreJobBusyRepaint();
                sInStep = 0;
                return 0;
            }
            if (Err != FAT_OK) {
                StoreInstallPumpAbort();
            }
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
        StoreJobBusyRepaint();
        sInStep = 0;
        return 0;

    case JP_FONT:
        StoreSetStatus("font/theme...");
        if (sBatch) {
            StoreComboBatchEnd();
            sBatch = 0;
        }
        StoreJobBusyRepaint();
        sPhase = JP_RELOAD;
        sInStep = 0;
        return 0;

    case JP_RELOAD:
        StoreSetStatus("reload...");
        GuiPollMouseMotion();
        Reload();
        GuiPollMouseMotion();
        DesktopNotifyAppsChanged();
        StoreJobBusyRepaint();
        sPhase = JP_FINISH;
        sInStep = 0;
        return 0;

    case JP_FINISH:
        sLastErr = sErr;
        StoreJobFinishStatus(sKind, sErr, sPlanN);
        StoreJobBusyRepaint();
        sRunning = 0;
        sPhase = JP_IDLE;
        sKind = STORE_JOB_NONE;
        sCancel = 0;
        sInStep = 0;
        return 1;

    default:
        StoreInstallPumpAbort();
        sRunning = 0;
        sPhase = JP_IDLE;
        sCancel = 0;
        sInStep = 0;
        return 1;
    }
}

int StoreJobUiIsBusy(void) {
    return (sRunning || sPendingKind != STORE_JOB_NONE) ? 1 : 0;
}

int StoreJobIsBusy(void) {
    return (StoreJobUiIsBusy() || StoreJobShellIsBusy()) ? 1 : 0;
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

int StoreJobLastError(void) {
    return sLastErr;
}

int StoreJobCancel(void) {
    if (StoreJobShellIsBusy() && !StoreJobUiIsBusy()) {
        return -1;
    }
    if (!StoreJobUiIsBusy()) {
        return -1;
    }
    sCancel = 1;
    if (!sRunning && sPendingKind != STORE_JOB_NONE) {
        sPendingKind = STORE_JOB_NONE;
        sCancel = 0;
        StoreSetStatus("cancelled");
        return 0;
    }
    return 0;
}
