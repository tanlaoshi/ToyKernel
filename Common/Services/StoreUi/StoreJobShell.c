/*
 * StoreJobShell.c — Shell ↔ StoreJob：Shell 只做 INTERFACE（rm-exc-11）
 *
 * 窗：Enqueue → 立即回 Gui 循环；WorkerTask Step。
 * Shell：Enqueue → 立即回提示符（勿 hlt 死等）；Worker 推进；查 store job。
 * 旧路径 Shell hlt 等 Job：Worker 一挂提示符永不通，与窗行为不一致。
 */
#include "StoreJob.h"
#include "Hal.h"

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

int StoreJobShellRun(STORE_JOB_KIND Kind, const char *Id) {
    if (sShellBusy) {
        return -1;
    }
    /* 只入队；完成看 StoreJobUiIsBusy / LastError / store job */
    return StoreJobEnqueue(Kind, Id);
}
