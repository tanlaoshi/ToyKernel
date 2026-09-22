/*
 * StoreJobShell.c — Shell 同步路径与 UI Job 互斥（PR-S-job-shell）
 */
#include "StoreJob.h"

static int sShellBusy;

int StoreJobShellBegin(void) {
    if (StoreJobUiIsBusy()) {
        return -1;
    }
    if (sShellBusy) {
        return -1;
    }
    sShellBusy = 1;
    return 0;
}

void StoreJobShellEnd(void) {
    sShellBusy = 0;
}

int StoreJobShellIsBusy(void) {
    return sShellBusy ? 1 : 0;
}
