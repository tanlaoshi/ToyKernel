/*
 * Kernel.c — 内核入口编排（PR-K-seq-1）
 *
 * 人话：Startup 填好 BOOT_INFO 后走进这里。这里只决定走哪条开机路径，
 *       再按顺序挂串口/屏、跑模块表、拉起 shell/gui/worker。
 *
 * 从哪读：KernelMain（文末，只分发）→ KernelMainVirt / VirtDesktop / Full
 *         → KernelMainCommon（桌面常驻任务）。
 *
 * 别改：三路旗标必须 HalConsoleOnly →（HasFrameBuffer 且 Virt 形状）→ Full。
 *       shell/gui/worker 失败即停；input 仅 SMP≥3，失败不停机。
 *
 * 想照着做：Documents/开发/代码可读性规范.md §2.4 / §2.5
 */
#include "BootInfo.h"
#include "ToySerialLog.h"
#include "Hal.h"
#include "HalSerial.h"
#include "Scheduler.h"
#include "KernelModules.h"
#include "Tasks.h"
#include "KernelTask.h"
#include "Console.h"
#include "Font.h"
#include "Theme.h"

static void KernelMainVirt(void);
static void KernelMainVirtDesktop(void);
static void KernelMainFull(void);
static void KernelMainCommon(void);
static void KernelParkForever(void);

static void KernelSpawnOrPark(const char *Name, void (*Entry)(void)) {
    if (SchedulerCreate(Name, Entry) < 0) {
        ToyLogBoot("kernel: SchedulerCreate failed\n");
        KernelParkForever();
    }
}

/* SMP≥3 才起 InputTask 钉 CPU2；失败只打日志（与旧 (void)KernelSpawn 相同）。 */
static void KernelTrySpawnInput(void) {
    if (HalCpuCount() <= 2) {
        return;
    }
    if (SchedulerCreate("input", InputTask) < 0) {
        ToyLogBoot("kernel: SchedulerCreate failed\n");
    }
}

/* 单核直跑串口壳；多核进调度让 AP idle。 */
static void KernelMainVirtEnterShell(void) {
    if (HalCpuCount() > 1) {
        KernelSpawnOrPark("shell", ConsoleSerialRun);
        SchedulerStart();
        return;
    }
    HalTimerStart();
    ConsoleSerialRun();
}

static void KernelParkForever(void) {
    for (;;) {
        HalCpuPark();
    }
}

static void KernelAppendDecimal4(char *Line, int *N, UINT32 Value) {
    Line[(*N)++] = (char)('0' + ((Value / 1000) % 10));
    Line[(*N)++] = (char)('0' + ((Value / 100) % 10));
    Line[(*N)++] = (char)('0' + ((Value / 10) % 10));
    Line[(*N)++] = (char)('0' + (Value % 10));
}

static void KernelLogFrameBufferSize(const BOOT_INFO *Info) {
    char Line[48];
    int N = 0;
    const char *Prefix = "Boot: FB ";

    while (*Prefix && N < 16) {
        Line[N++] = *Prefix++;
    }
    KernelAppendDecimal4(Line, &N, Info->HorizontalResolution);
    Line[N++] = 'x';
    KernelAppendDecimal4(Line, &N, Info->VerticalResolution);
    Line[N++] = '\n';
    Line[N] = 0;
    ToyLogBoot(Line);
}

/*
 * 尽早挂串口和帧缓冲：后面模块 ConsoleWrite 若 Width=0 会空转。
 * 有 FB 则开 GOP 镜像（接 ToyBoot 黑底，禁止再全屏 Clear）。
 */
static void KernelAttachEarlyVideo(void) {
    const BOOT_INFO *Info = BootInfoGet();
    VIDEO_CONFIG Video = BootInfoToVideoConfig(Info);

    HalSerialInitialize();
    HalVideoSet(&Video);
    if (Info && Info->FrameBufferSize != 0) {
        FontInitialize();
        ThemeInitialize();
        HalSerialGopEnable();
        ToyLogBoot("Boot: KernelMain Live\n");
        KernelLogFrameBufferSize(Info);
    }
}

static void KernelAfterModules(void) {
    (void)FontSetById(ThemeFontId());
    HalSerialGopMirror(0);
}

static void KernelRunOrPark(int (*RunModules)(void)) {
    if (RunModules() != 0) {
        KernelParkForever();
    }
}

/* 串口子集。 */
static void KernelMainVirt(void) {
    KernelAttachEarlyVideo();
    KernelRunOrPark(KernelModulesRunVirt);
    KernelAfterModules();
    KernelMainVirtEnterShell();
}

/* 桌面常驻：只顺序 Create / 演示任务 / 可选 input / Start。 */
static void KernelMainCommon(void) {
    KernelSpawnOrPark("shell", ShellTask);
    KernelSpawnOrPark("gui", GuiTask);
    KernelSpawnOrPark("worker", WorkerTask);
    KernelTaskDemoStart();
    KernelTrySpawnInput();
    SchedulerStart();
}

static void KernelMainVirtDesktop(void) {
    KernelAttachEarlyVideo();
    KernelRunOrPark(KernelModulesRunVirtDesktop);
    KernelAfterModules();
    KernelMainCommon();
}

static void KernelMainFull(void) {
    KernelAttachEarlyVideo();
    KernelRunOrPark(KernelModulesRunFull);
    KernelAfterModules();
    KernelMainCommon();
}

void KernelMain(void) {
    if (HalConsoleOnly()) {
        KernelMainVirt();
        return;
    }
    if (HalHasFrameBuffer() && HalPlatformIsVirtSerialConsole()) {
        KernelMainVirtDesktop();
        return;
    }
    KernelMainFull();
}
