/*
 * KernelModules.c — 子系统模块初始化表
 */
#include "KernelModules.h"
#include "Module.h"
#include "BootInfo.h"
#include "Hal.h"
#include "UI.h"
#include "Console.h"
#include "FileSystem.h"
#include "Scheduler.h"
#include "PhysicalMemory.h"
#include "VirtualMemory.h"
#include "Gui.h"
#include "Udp.h"
#include "Tcp.h"
#include "ShellCommands.h"
#include "Debug.h"
#include "Font.h"
#include "Theme.h"
#include "Db.h"
#include "Locale.h"
#include "Driver.h"

static int gVirtDesktop; /* PR-V5/B1：已选桌面模块表（有 FB 且非 ConsoleOnly） */

static void VirtualMemoryMapIdentity(UINT64 Phys, UINT64 Size) {
    if (Size == 0) {
        return;
    }
    UINT64 Start = Phys & ~(UINT64)(PAGE_SIZE - 1);
    UINT64 End = Phys + Size;
    while (Start < End) {
        VirtualMemoryMapPage(Start, Start, PTE_PRESENT | PTE_WRITABLE);
        Start += PAGE_SIZE;
    }
}

static int InitializeSerial(void) {
    HalSerialInit();
    return 0;
}

static int InitializePhysicalMemory(void) {
    return PhysicalMemoryInit();
}

static int InitializeVirtualMemory(void) {
    const BOOT_INFO *Info = BootInfoGet();

    if (VirtualMemoryInit() != 0) {
        return -1;
    }
    if (Info && Info->FrameBufferSize != 0) {
        VirtualMemoryMapIdentity(Info->FrameBufferBase, Info->FrameBufferSize);
    }
    HalPlatformMapMmio();
    VirtualMemoryEnable();
    return 0;
}

static int InitializeVideo(void) {
    const BOOT_INFO *Info = BootInfoGet();
    VIDEO_CONFIG V = BootInfoToVideoConfig(Info);

    FontInit();
    ThemeInit();
    HalVideoSet(&V);
    HalVideoInitBackbuffer();
    HalVideoClearScreen(ThemeDesktopBg());
    HalVideoPresent();
    /* PR-H3：无 COM1 时把串口缓冲刷到帧缓冲文字 */
    HalSerialGopEnable();
    return 0;
}

static int InitializeCpu(void) {
    if (HalInit() != 0) {
        return -1;
    }
    HalTimerInit();
    HalSyscallInit();
    /* PR-V3：virtio-input；失败可无头继续（仍有串口） */
    (void)HalUsbInit();
    return 0;
}

static int InitializeSmp(void) {
    return HalSmpStartApplicationProcessors();
}

static int InitializeUsb(void) {
    return HalUsbInit();
}

static int InitializeFileSystem(void) {
    return FileSystemInit();
}

static int InitializeGui(void) {
    (void)DbInit();
    (void)FontLoadAssets(); /* PR-T3：须在 ThemeLoad 前，便于 font= 选中运行时 id */
    (void)ThemeLoad();
    LocaleInit();
    GuiInit();
    return 0;
}

static int InitializeNetwork(void) {
    if (HalNetInit() != 0) {
        return -1;
    }
    UdpInit();
    TcpInit();
    return 0;
}

static int InitializeDriver(void) {
    /* PR-D2：先注册平台驱动；ProbeAll 可早绑 ATA；virtio-blk 待 VMM 后由 HalBlockInit 再 Probe */
    HalDriverRegister();
    return ToyDriverProbeAll();
}

static int InitializeScheduler(void) {
    SchedulerInit();
    return 0;
}

static int InitializeConsole(void) {
    LocaleInit();
    ConsoleRegisterBuiltins();
    if (HalConsoleOnly()) {
        ShellCommandsRegisterVirtMin();
    } else {
        ShellCommandsRegister();
    }
    ConsoleUserAliasLoad();
    ConsoleInit();
    return 0;
}

/* x86 全量桌面路径 */
static const MODULE gModulesFull[] = {
    { "serial",  InitializeSerial },
    { "memory",     InitializePhysicalMemory },
    { "driver",     InitializeDriver },
    { "virtual-memory",     InitializeVirtualMemory },
    { "video",   InitializeVideo },
    { "cpu",     InitializeCpu },
    { "smp",     InitializeSmp },
    { "file-system",      InitializeFileSystem },
    { "usb",     InitializeUsb },
    { "network",     InitializeNetwork },
    { "gui",     InitializeGui },
    { "scheduler",   InitializeScheduler },
    { "console", InitializeConsole },
};

/* PR-A8 / B1：HalConsoleOnly — 串口子集（无 FB / 命令行靶） */
static const MODULE gModulesVirt[] = {
    { "serial",  InitializeSerial },
    { "memory",     InitializePhysicalMemory },
    { "driver",     InitializeDriver },
    { "virtual-memory",     InitializeVirtualMemory },
    { "cpu",     InitializeCpu },
    { "smp",     InitializeSmp },
    { "scheduler",   InitializeScheduler },
    { "console", InitializeConsole },
};

/* PR-V5/N10/A14：virt 桌面（A14 挂 smp；输入在 InitializeCpu；N10 挂 net） */
static const MODULE gModulesVirtDesktop[] = {
    { "serial",  InitializeSerial },
    { "memory",     InitializePhysicalMemory },
    { "driver",     InitializeDriver },
    { "virtual-memory",     InitializeVirtualMemory },
    { "video",   InitializeVideo },
    { "cpu",     InitializeCpu },
    { "smp",     InitializeSmp },
    { "file-system",      InitializeFileSystem },
    { "network",     InitializeNetwork },
    { "gui",     InitializeGui },
    { "scheduler",   InitializeScheduler },
    { "console", InitializeConsole },
};

int KernelModulesVirtDesktop(void) {
    return gVirtDesktop;
}

int KernelModulesRun(void) {
    /*
     * PR-B1：用能力旗标选表，不再「凡非 x86 即 virt 串口」。
     * ConsoleOnly → 串口子集；HasFrameBuffer + virt 形状 → virt 桌面；否则 x86 全表。
     */
    if (HalConsoleOnly()) {
        gVirtDesktop = 0;
        return ModulesRun(gModulesVirt,
                          (int)(sizeof(gModulesVirt) / sizeof(gModulesVirt[0])));
    }
    if (HalHasFrameBuffer() && HalPlatformVirtConsole()) {
        gVirtDesktop = 1;
        return ModulesRun(gModulesVirtDesktop,
                          (int)(sizeof(gModulesVirtDesktop) /
                                sizeof(gModulesVirtDesktop[0])));
    }
    gVirtDesktop = HalHasFrameBuffer() ? 1 : 0;
    return ModulesRun(gModulesFull,
                      (int)(sizeof(gModulesFull) / sizeof(gModulesFull[0])));
}
