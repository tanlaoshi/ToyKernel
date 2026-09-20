/*
 * Kernel.c — 内核入口：早期 Video 设置、模块初始化、启动常驻任务
 */
#include "BootInfo.h"
#include "ToySerialLog.h"
#include "Hal.h"
#include "HalSerial.h"
#include "Scheduler.h"
#include "KernelModules.h"
#include "Tasks.h"
#include "Console.h"
#include "Font.h"
#include "Theme.h"

void KernelMain(void) {
    const BOOT_INFO *Info = BootInfoGet();
    VIDEO_CONFIG V = BootInfoToVideoConfig(Info);

    /* 防御：非 UEFI 入口路径也保证串口已 Initialize（幂等） */
    HalSerialInitialize();

    /* 尽早挂上帧缓冲，避免 mem 等模块 ConsoleWrite 时 Width=0 死循环 */
    HalVideoSet(&V);
    /*
     * PR-K-log-cont：接 ToyBoot 已清的黑底，禁止再全屏 Clear（否则闪黑/日志断层）。
     * 有 FB 则开 GOP 镜像：无 COM1 真机也能看见 [Mod]；有串口则 UART+屏同文。
     */
    if (Info && Info->FrameBufferSize != 0) {
        FontInitialize(); /* GOP 日志/DrawString 依赖字体表；video 模块里会再 Init 一次 */
        /*
         * ThemeInitialize 记桌面默认 font=2；GopEnable 按分辨率套 boot 大字
         *（PR-K-log-4kfont）。Video 再 Init 时 Theme/GopEnable 会保持同一套。
         */
        ThemeInitialize();
        HalSerialGopEnable();
        ToyLogBoot("Boot: KernelMain Live\n");
        {
            char Line[48];
            int n = 0;
            const char *P = "Boot: FB ";
            UINT32 W = Info->HorizontalResolution;
            UINT32 H = Info->VerticalResolution;
            while (*P && n < 16) {
                Line[n++] = *P++;
            }
            Line[n++] = (char)('0' + ((W / 1000) % 10));
            Line[n++] = (char)('0' + ((W / 100) % 10));
            Line[n++] = (char)('0' + ((W / 10) % 10));
            Line[n++] = (char)('0' + (W % 10));
            Line[n++] = 'x';
            Line[n++] = (char)('0' + ((H / 1000) % 10));
            Line[n++] = (char)('0' + ((H / 100) % 10));
            Line[n++] = (char)('0' + ((H / 10) % 10));
            Line[n++] = (char)('0' + (H % 10));
            Line[n++] = '\n';
            Line[n] = 0;
            ToyLogBoot(Line);
        }
    }

    if (KernelModulesRun() != 0) {
        for (;;) {
            HalCpuPark();
        }
    }

    /*
     * 进调度/桌面前：套用 ThemeLoad 选中的字面，再关掉 boot→GOP 镜像
     * （桌面勿被串口字盖住；boot 上滚用 HalSerialBootFontApply，与桌面字面分离）。
     */
    (void)FontSetById(ThemeFontId());
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
        /* PR-S-input-pin 序 2：SMP≥3 才起 InputTask 钉 CPU2；SMP=2 留序 1 等价 yield-path drain */
        if (HalCpuCount() > 2) {
            SchedulerCreate("input", InputTask);
        }
        SchedulerStart();
        return;
    }

    SchedulerCreate("shell", ShellTask);
    SchedulerCreate("gui", GuiTask);
    SchedulerCreate("worker", WorkerTask);
    if (HalCpuCount() > 2) {
        SchedulerCreate("input", InputTask);
    }
    SchedulerStart();
}
