/*
 * StoreJob.c — Store UI 作业状态机（PR-S-job）
 * 序 1：Step = 一次跑完旧 Combo/Sync + Reload/Notify。
 * 序 2：Busy 阶段文案（含 id）；相位/chunk 后续刀。
 */
#include "StoreUiPrivate.h"
#include "StoreJob.h"

static STORE_JOB_KIND sPendingKind;
static int sRunning;
static char sJobId[STORE_ID_MAX];

static void StatusWithId(const char *Verb, const char *Id) {
    char Buf[80];
    int i = 0;
    int j;

    if (!Verb) {
        Verb = "?";
    }
    while (Verb[i] && i < 24) {
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

static void BusyRepaint(void) {
    if (StoreUiIsFocused()) {
        StoreUiRepaint();
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
        StatusWithId("install", sJobId);
        BusyRepaint();
        Err = StoreComboInstall(sJobId);
    } else if (Job == STORE_JOB_REMOVE) {
        StatusWithId("remove", sJobId);
        BusyRepaint();
        Err = StoreComboRemove(sJobId);
    } else {
        StoreSetStatus("sync: catalog");
        BusyRepaint();
        Err = StoreSyncCatalog();
    }

    StoreSetStatus("reload...");
    BusyRepaint();
    GuiPollMouseMotion();
    Reload();
    GuiPollMouseMotion();
    DesktopNotifyAppsChanged();
    if (Err == 0) {
        if (Job == STORE_JOB_INSTALL) {
            StoreSetStatus("installed");
        } else if (Job == STORE_JOB_REMOVE) {
            StoreSetStatus("removed");
        } else {
            StoreSetStatus("sync ok");
        }
    } else if (Job == STORE_JOB_INSTALL) {
        StoreSetStatus("install fail");
    } else if (Job == STORE_JOB_REMOVE) {
        StoreSetStatus("remove fail");
    } else {
        StoreSetStatus("sync fail (need repo)");
    }
    BusyRepaint();
    sRunning = 0;
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
