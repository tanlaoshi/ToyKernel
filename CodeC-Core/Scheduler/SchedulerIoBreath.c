/*
 * SchedulerIoBreath.c — PR-K-preempt-breath：统一长 IO 呼吸
 *
 * 长 Store/FAT/Present/HTTP 路径改调本函数；稳态仍走 YieldForPollInput。
 * 5i ❌ 曾去掉 CondResched；已恢复。5j 不改本文件。
 */
#include "Scheduler.h"
#include "Hal.h"
#include "Gui.h"

void SchedulerIoBreath(void) {
    UINT64 Flags;

    /*
     * 长 IO 在 Worker 上跑时 GuiTask 可自由 Poll；此处仍 drain + 光标位移，
     * 并置 NeedResched，让 CondResched 浅让出拍给 Gui（装卸期鼠标跟手）。
     */
    SchedulerPreemptDisable();
    Flags = HalIrqSave();
    HalInputPoll();
    GuiPollMouseMotion();
    HalIrqRestore(Flags);
    SchedulerPreemptEnable();
    SchedulerSetNeedResched();
    (void)SchedulerCondResched();
}
