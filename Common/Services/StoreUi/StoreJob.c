/*
 * StoreJob.c — Store UI 作业状态机（PR-S-job-api）
 * 首版 Step = 一次跑完旧 Combo/Sync + Reload/Notify（同原 StoreUiPump）。
 */
#include "StoreUiPrivate.h"
#include "StoreJob.h"

static STORE_JOB_KIND sPendingKind;
static int sRunning;
static char sJobId[STORE_ID_MAX];

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
    STORE_JOB_KIND Job;
    int Err;

    if (sRunning) {
        return 0;
    }
    if (sPendingKind == STORE_JOB_NONE) {
        return 1;
    }

    Job = sPendingKind;
    sPendingKind = STORE_JOB_NONE;
    sRunning = 1;

    if (Job == STORE_JOB_INSTALL) {
        Err = StoreComboInstall(sJobId);
        StoreSetStatus(Err == 0 ? "installed" : "install fail");
    } else if (Job == STORE_JOB_REMOVE) {
        Err = StoreComboRemove(sJobId);
        StoreSetStatus(Err == 0 ? "removed" : "remove fail");
    } else {
        Err = StoreSyncCatalog();
        StoreSetStatus(Err == 0 ? "sync ok" : "sync fail (need repo)");
    }
    GuiPollMouseMotion();
    Reload();
    GuiPollMouseMotion();
    DesktopNotifyAppsChanged();
    if (StoreUiIsFocused()) {
        StoreUiRepaint();
    }
    sRunning = 0;
    (void)Err;
    return 1;
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
