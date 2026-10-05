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
 *       各路径只调对应 KernelModulesRunVirt / RunVirtDesktop / RunFull。
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

/* SchedulerCreate 成功返回槽号（≥0），失败 -1——勿用 !=0。 */
static int KernelSpawn(const char *Name, void (*Entry)(void)) {
    if (SchedulerCreate(Name, Entry) < 0) {
        ToyLogBoot("kernel: SchedulerCreate failed\n");
        return -1;
    }
    return 0;
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

/* 串口子集：单核直跑壳；多核 spawn 后 SchedulerStart 让 AP 进 idle。 */
static void KernelMainVirt(void) {
    KernelAttachEarlyVideo();
    KernelRunOrPark(KernelModulesRunVirt);
    KernelAfterModules();
    if (HalCpuCount() > 1) {
        if (KernelSpawn("shell", ConsoleSerialRun) != 0) {
            KernelParkForever();
        }
        SchedulerStart();
        return;
    }
    HalTimerStart();
    ConsoleSerialRun();
}

/* 桌面常驻任务（virt 桌面与 x86 全量相同）。CPU>2 才起 input。 */
static void KernelMainCommon(void) {
    if (KernelSpawn("shell", ShellTask) != 0 ||
        KernelSpawn("gui", GuiTask) != 0 ||
        KernelSpawn("worker", WorkerTask) != 0) {
        KernelParkForever();
    }
    KernelTaskDemoStart();
    if (HalCpuCount() > 2) {
        (void)KernelSpawn("input", InputTask);
    }
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
