/*
 * Tasks.c — GUI / Worker / 输入专核（PR-S-tasks-1）
 */
#include "Tasks.h"
#include "TasksPrivate.h"
#include "Hal.h"
#include "HalVideo.h"
#include "HIDKeyboard.h"
#include "Console.h"
#include "Gui.h"
#include "SettingsUi.h"
#include "FilesUi.h"
#include "EditUi.h"
#include "Udp.h"
#include "Tcp.h"
#include "LwIp.h"
#include "Debug.h"
#include "ShellCommands.h"
#include "ToySerialLog.h"
#include "Scheduler.h"
#include "StoreJob.h"

static volatile UINT32 gWorkerCount;

UINT32 WorkerLoopCount(void) {
    return gWorkerCount;
}

/*
 * 真机 poll-USB：纯 hlt 要等 PIT/HPET tick 才醒 → 光标更新锁在 ~10ms+，体感极卡。
 * 多数轮次短自旋；偶发 hlt 仍给定时器/短按电源窗口。
 */
void YieldForPollInput(void) {
    /*
     * PR-S-compose-sep（序 4）：单一 drain 主人——反例禁止「专核空转却仍在 shell
     * 同步 HalInputPoll 双路径抢设备」。故按 InputTask 是否存在分域：
     * - SMP<3（无 InputTask）：此处为稳态 drain 主人（序 1 等价）。
     * - SMP≥3（InputTask 钉 CPU2 持续 drain）：InputTask 为单一主人，此处不 drain，
     *   只让步（pause/hlt），避双路径抢 xHCI 设备。
     * 长 Store IO 不走此路径，由 StoreIoBreath 自带 drain 兜底（IO 呼吸，非稳态）。
     */
    if (HalCpuCount() < 3) {
        HalInputPoll();
    }
    if (!HalCpuIsHypervisor()) {
        UINT32 i;
        if (HalPowerButtonPressed()) {
            ToyLogBoot("Boot: Power Button -> Shutdown\n");
            HalCpuShutdown();
        }
        /* 真机 poll-USB：勿 hlt 等 tick，否则光标锁 ~10ms+ */
        for (i = 0; i < 200; i++) {
            HalCpuRelax();
        }
        (void)SchedulerCondResched();
        return;
    }
    HalCpuHalt();
    (void)SchedulerCondResched();
}

void GuiTask(void) {
    for (;;) {
        GuiPollMouse();
        YieldForPollInput();
    }
}

/*
 * StoreJob 后台泵：窗/Shell 只 Enqueue；本任务 Step。
 * 勿再在 GuiPollMouse→StoreUiPump 里 Step，否则 Gui 被 StoreRemove 堵住，装卸期鼠标必卡。
 */
void WorkerTask(void) {
    for (;;) {
        gWorkerCount++;
        if (StoreJobUiIsBusy()) {
            (void)StoreJobStep();
            SchedulerIoBreath();
            continue;
        }
        HalCpuHalt();
        (void)SchedulerCondResched();
    }
}

/*
 * PR-S-input-pin（序 2）：输入钉专核（SMP≥3 → 逻辑 CPU2）。
 *
 * scoped sti（关键）：sti 只在浅栈的 200-pause 期开；HalInputPoll 期 cli 关中断。
 * 原因：HalInputPoll → XchiDrainEvents → ProcessEvents* 调用链深，若被定时器在
 * 深调用中抢占，ISR 链（InterruptDispatch→SchedulerOnTimer→PickNext）压在深栈之上，
 * 实测会损坏 iret 帧（rip/rsp 变 gTasks 栈址）→ #UD/#GP。worker 之所以稳，是它只在
 * hlt（浅栈）被抢。故 InputTask 须只在浅 pause 处被抢：cli 守 drain，sti 放 pause。
 * 其专核上只有 InputTask+idle，sti 不外溢到 shell/gui 协作态（CPU1 仍 IF=0）。
 *
 * 并发安全：XchiDrainEvents 已 gEvtConsumerLock/gHidQueueLock 串行 + 命令期
 * XhciEventIsExclusive 早退 → 与 shell/gui 的 yield-path drain 并发安全（序 1 已证）。
 */
void InputTask(void) {
    for (;;) {
        HalIrqDisable();   /* drain 期关中断：勿在深调用中被抢 */
        HalInputPoll();
        HalIrqEnable();    /* pause 期开中断：浅栈处可被定时器抢 → ticks>0 */
        {
            UINT32 i;
            for (i = 0; i < 200; i++) {
                HalCpuRelax();
            }
        }
        (void)SchedulerCondResched();
    }
}
