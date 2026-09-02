/*
 * Kernel.c — 内核入口：早期 Video 设置、模块初始化、启动常驻任务
 */
#include "BootInfo.h"
#include "Hal.h"
#include "Scheduler.h"
#include "KernelModules.h"
#include "Tasks.h"

void KernelMain(void) {
    const BOOT_INFO *Info = BootInfoGet();
    VIDEO_CONFIG V = BootInfoToVideoConfig(Info);

    /* 尽早挂上帧缓冲，避免 mem 等模块 ConsoleWrite 时 Width=0 死循环 */
    HalVideoSet(&V);

    if (KernelModulesRun() != 0) {
        for (;;) {
            HalCpuPark();
        }
    }

    SchedulerCreate("shell", ShellTask);
    SchedulerCreate("gui", GuiTask);
    SchedulerCreate("worker", WorkerTask);
    SchedulerStart();
}
