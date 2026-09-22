/*
 * SchedulerIoBreath.c — PR-K-preempt-breath：统一长 IO 呼吸
 *
 * 长 Store/FAT/Present/HTTP 路径改调本函数；稳态仍走 YieldForPollInput。
 * CondResched 归刀 4（本刀只留挂点注释，不切任务）。
 */
#include "Scheduler.h"
#include "Hal.h"
#include "Gui.h"

void SchedulerIoBreath(void) {
    UINT64 Flags;

    /*
     * 长 IO 期间 GuiTask 常不走 YieldForPollInput；真机 MSC/FAT 完成事件
     * 需 XchiDrainEvents → 此处始终 HalInputPoll（与稳态 SMP≥3 单主人不同）。
     */
    SchedulerPreemptDisable();
    Flags = HalIrqSave();
    HalInputPoll();
    GuiPollMouseMotion();
    HalIrqRestore(Flags);
    SchedulerPreemptEnable();
    (void)SchedulerCondResched();
}
