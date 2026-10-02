/*
 * StoreJobStep.c — Store 作业步进状态机（PR-S3-storejob-1）
 *
 * 从 StoreJob.c 原样搬家；不改语义。作业态见 StoreJob.c。
 */
#include "StoreUiPrivate.h"
#include "StoreJob.h"
#include "Fat.h"
#include "HalConsole.h"

int StoreJobStep(void) {
    int Err;

    if (gJobInStep) {
        return gJobRunning ? 0 : 1;
    }
    gJobInStep = 1;

    if (!gJobRunning) {
        if (gJobPendingKind == STORE_JOB_NONE) {
            gJobInStep = 0;
            return 1;
        }
        if (gJobCancel) {
            gJobPendingKind = STORE_JOB_NONE;
            gJobCancel = 0;
            StoreSetStatus("cancelled");
            StoreJobBusyRepaint();
            gJobInStep = 0;
            return 1;
        }
        gJobKind = gJobPendingKind;
        gJobPendingKind = STORE_JOB_NONE;
        gJobRunning = 1;
        gJobPhase = JP_PLAN;
        gJobErr = FAT_OK;
        gJobPlanN = 0;
        gJobPlanI = 0;
        gJobBatch = 0;
    }

    if (gJobCancel && gJobRunning) {
        JobApplyCancel();
    }

    switch (gJobPhase) {
    case JP_PLAN:
        if (gJobKind == STORE_JOB_SYNC) {
            StoreSetStatus("sync: catalog");
            StoreJobBusyRepaint();
            gJobPhase = JP_PKG;
            gJobInStep = 0;
            return 0;
        }
        if (gJobKind == STORE_JOB_FETCH) {
            StoreJobStatusWithId("fetch", gJobId);
            StoreJobBusyRepaint();
            gJobPhase = JP_PKG;
            gJobInStep = 0;
            return 0;
        }
        StoreComboBatchBegin();
        gJobBatch = 1;
        StoreJobStatusWithId("plan", gJobId);
        StoreJobBusyRepaint();
        if (gJobKind == STORE_JOB_INSTALL) {
            Err = StoreComboPlanInstall(gJobId, gJobPlan, STORE_ENTRIES_MAX,
                                        &gJobPlanN);
        } else {
            Err = StoreComboPlanRemove(gJobId, gJobPlan, STORE_ENTRIES_MAX,
                                      &gJobPlanN);
        }
        if (Err != FAT_OK) {
            gJobErr = Err;
            gJobPhase = JP_FONT;
            gJobInStep = 0;
            return 0;
        }
        gJobPlanI = 0;
        if (gJobPlanN > 0) {
            gJobPhase = JP_PKG;
        } else if (gJobKind == STORE_JOB_INSTALL) {
            StoreSetStatus("already installed");
            StoreJobBusyRepaint();
            gJobPhase = JP_FONT;
        } else {
            gJobPhase = JP_FONT;
        }
        gJobInStep = 0;
        return 0;

    case JP_PKG:
        if (gJobCancel) {
            JobApplyCancel();
            gJobInStep = 0;
            return 0;
        }
        if (gJobKind == STORE_JOB_SYNC) {
            Err = StoreSyncCatalog();
            if (Err != 0) {
                gJobErr = Err;
            }
            gJobPhase = JP_RELOAD;
            gJobInStep = 0;
            return 0;
        }
        if (gJobKind == STORE_JOB_FETCH) {
            Err = StoreFetchId(gJobId);
            if (Err != 0) {
                gJobErr = Err;
            }
            /* 仅写缓存，不必 font/reload */
            gJobPhase = JP_FINISH;
            gJobInStep = 0;
            return 0;
        }
        if (gJobPlanI >= gJobPlanN) {
            gJobPhase = JP_FONT;
            gJobInStep = 0;
            return 0;
        }
        if (!gJobPlan[gJobPlanI][0]) {
            gJobPlanI++;
            if (gJobPlanI >= gJobPlanN) {
                gJobPhase = JP_FONT;
            }
            gJobInStep = 0;
            return 0;
        }
        StoreJobStatusProgress(gJobKind == STORE_JOB_INSTALL ? "install" : "remove",
                               gJobPlan[gJobPlanI], gJobPlanI + 1, gJobPlanN);
        if (gJobKind == STORE_JOB_INSTALL) {
            if (!StoreInstallPumpBusy()) {
                HalConsoleWriteSerial("store combo: +");
                HalConsoleWriteSerial(gJobPlan[gJobPlanI]);
                HalConsoleWriteSerial("\n");
                Err = StoreInstallPump(gJobPlan[gJobPlanI]);
            } else {
                Err = StoreInstallPump(0);
            }
            if (Err == 1) {
                UINTN Got = 0;
                UINTN Sz = 0;

                if (gJobCancel) {
                    JobApplyCancel();
                    gJobInStep = 0;
                    return 0;
                }
                StoreInstallPumpProgress(&Got, &Sz);
                StoreJobStatusCopy(gJobPlan[gJobPlanI], Got, Sz);
                StoreJobBusyRepaint();
                gJobInStep = 0;
                return 0;
            }
            if (Err != FAT_OK) {
                StoreInstallPumpAbort();
            }
        } else {
            HalConsoleWriteSerial("store uncombo: -");
            HalConsoleWriteSerial(gJobPlan[gJobPlanI]);
            HalConsoleWriteSerial("\n");
            Err = StoreRemove(gJobPlan[gJobPlanI]);
            if (gJobPlanI > 0 && (Err == FAT_ERR_INVAL || Err == FAT_ERR_NOENT)) {
                Err = FAT_OK;
            }
        }
        gJobPlanI++;
        if (Err != FAT_OK) {
            gJobErr = Err;
            gJobPhase = JP_FONT;
        } else if (gJobPlanI >= gJobPlanN) {
            gJobPhase = JP_FONT;
        }
        StoreJobBusyRepaint();
        gJobInStep = 0;
        return 0;

    case JP_FONT:
        StoreSetStatus("font/theme...");
        if (gJobBatch) {
            StoreComboBatchEnd();
            gJobBatch = 0;
        }
        StoreJobBusyRepaint();
        gJobPhase = JP_RELOAD;
        gJobInStep = 0;
        return 0;

    case JP_RELOAD:
        StoreSetStatus("reload...");
        GuiPollMouseMotion();
        Reload();
        GuiPollMouseMotion();
        DesktopNotifyAppsChanged();
        StoreJobBusyRepaint();
        gJobPhase = JP_FINISH;
        gJobInStep = 0;
        return 0;

    case JP_FINISH:
        gJobLastErr = gJobErr;
        StoreJobFinishStatus(gJobKind, gJobErr, gJobPlanN);
        StoreJobBusyRepaint();
        gJobRunning = 0;
        gJobPhase = JP_IDLE;
        gJobKind = STORE_JOB_NONE;
        gJobCancel = 0;
        gJobInStep = 0;
        return 1;

    default:
        StoreInstallPumpAbort();
        gJobRunning = 0;
        gJobPhase = JP_IDLE;
        gJobCancel = 0;
        gJobInStep = 0;
        return 1;
    }
}
