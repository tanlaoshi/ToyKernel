/*
 * StoreJobShell.c — Shell ↔ UI Job：同一套 StoreJob，Shell 只是 INTERFACE
 *
 * 窗：Enqueue → GuiPollMouse→Pump。
 * Shell：StoreJobShellRun → Enqueue + 浅等（GuiTask 仍 Pump）；等期间 Pause PresentDefer。
 */
#include "StoreJob.h"
#include "Hal.h"
#include "Gui.h"

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
    if (StoreJobEnqueue(Kind, Id) != 0) {
        return -1;
    }
    /*
     * 与 Store 窗同一 Job：本核不 Step（避 ConsoleOnEnter 深栈）。
     * Pause Defer：ConsoleOnEnter 的 Push 否则全局禁 Present → Shell 等 Job 时光标假死。
     */
    GuiPresentDeferPause();
    while (StoreJobUiIsBusy()) {
        HalIrqEnable();
        HalCpuHalt();
    }
    GuiPresentDeferResume();
    return 0;
}
