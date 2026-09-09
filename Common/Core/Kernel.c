/*
 * Kernel.c — 内核入口：早期 Video 设置、模块初始化、启动常驻任务
 */
#include "BootInfo.h"
#include "Hal.h"
#include "HalSerial.h"
#include "Scheduler.h"
#include "KernelModules.h"
#include "Tasks.h"
#include "Console.h"
#include "Font.h"

void KernelMain(void) {
    const BOOT_INFO *Info = BootInfoGet();
    VIDEO_CONFIG V = BootInfoToVideoConfig(Info);

    /* 尽早挂上帧缓冲，避免 mem 等模块 ConsoleWrite 时 Width=0 死循环 */
    HalVideoSet(&V);
    /* H0：进核即改像素（在开分页 / 驱动 Probe 之前），真机卡死时可区分 Boot vs Kernel */
    if (Info && Info->FrameBufferSize != 0) {
        FontInit(); /* GOP 日志/DrawString 依赖字体表；video 模块里会再 Init 一次 */
        HalVideoClearScreen(0x00204060u);
        HalVideoPresent();
        HalSerialGopEnable();
        HalSerialWrite("boot: KernelMain live\n");
    }

    if (KernelModulesRun() != 0) {
        for (;;) {
            HalCpuPark();
        }
    }

    /*
     * 进调度/桌面前关掉 boot→GOP 镜像：之后 Debug 只进 ring，
     * 有 COM1 再旁路写串口。有/无串口主路径一致。
     */
    HalSerialGopMirror(0);

    /* PR-B1：ConsoleOnly → 串口壳；HasFrameBuffer + virt 形状 → 协作桌面 */
    if (HalConsoleOnly()) {
        /* PR-A14：多核时也走 SchedulerStart，让 AP 进 idle；单核仍直跑串口壳 */
        if (HalCpuCount() > 1) {
            SchedulerCreate("shell", ConsoleSerialRun);
            SchedulerStart();
            return;
        }
        HalTimerStart();
        ConsoleSerialRun();
        return;
    }

    if (HalHasFrameBuffer() && HalPlatformIsVirtSerialConsole()) {
        SchedulerCreate("shell", ShellTask);
        SchedulerCreate("gui", GuiTask);
        SchedulerCreate("worker", WorkerTask);
        SchedulerStart();
        return;
    }

    SchedulerCreate("shell", ShellTask);
    SchedulerCreate("gui", GuiTask);
    SchedulerCreate("worker", WorkerTask);
    SchedulerStart();
}
