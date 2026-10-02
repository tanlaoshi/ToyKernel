/*
 * StoreJob.c — Store UI 作业入队 / 忙态 / 取消（PR-S3-storejob-1）
 *
 * 步进状态机见 StoreJobStep.c。
 */
#include "StoreUiPrivate.h"
#include "StoreJob.h"
#include "Fat.h"

STORE_JOB_KIND gJobPendingKind;
STORE_JOB_KIND gJobKind;
STORE_JOB_PHASE gJobPhase;
int gJobRunning;
int gJobInStep;
int gJobCancel;
char gJobId[STORE_ID_MAX];
char gJobPlan[STORE_ENTRIES_MAX][STORE_ID_MAX];
int gJobPlanN;
int gJobPlanI;
int gJobErr;
int gJobLastErr;
int gJobBatch;

void JobApplyCancel(void) {
    StoreInstallPumpAbort();
    if (gJobBatch) {
        StoreComboBatchEnd();
        gJobBatch = 0;
    }
    gJobErr = STORE_JOB_ERR_CANCEL;
    gJobCancel = 0;
    gJobPhase = JP_RELOAD;
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
        gJobId[0] = 0;
    } else {
        if (!Id || !Id[0]) {
            return -1;
        }
        for (i = 0; Id[i] && i < STORE_ID_MAX - 1; i++) {
            gJobId[i] = Id[i];
        }
        gJobId[i] = 0;
    }
    gJobPendingKind = Kind;
    gJobCancel = 0;
    return 0;
}

int StoreJobUiIsBusy(void) {
    return (gJobRunning || gJobPendingKind != STORE_JOB_NONE) ? 1 : 0;
}

int StoreJobIsBusy(void) {
    return (StoreJobUiIsBusy() || StoreJobShellIsBusy()) ? 1 : 0;
}

int StoreJobIsRunning(void) {
    return gJobRunning ? 1 : 0;
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
    return gJobLastErr;
}

int StoreJobCancel(void) {
    if (StoreJobShellIsBusy() && !StoreJobUiIsBusy()) {
        return -1;
    }
    if (!StoreJobUiIsBusy()) {
        return -1;
    }
    gJobCancel = 1;
    if (!gJobRunning && gJobPendingKind != STORE_JOB_NONE) {
        gJobPendingKind = STORE_JOB_NONE;
        gJobCancel = 0;
        StoreSetStatus("cancelled");
        return 0;
    }
    return 0;
}
