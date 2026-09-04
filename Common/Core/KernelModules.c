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

static int gVirtDesktop; /* PR-V5：有帧缓冲则走桌面模块表 */

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

static int InitSerial(void) {
    HalSerialInit();
    return 0;
}

static int InitMem(void) {
    return PhysicalMemoryInit();
}

static int InitVmm(void) {
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

static int InitVideo(void) {
    const BOOT_INFO *Info = BootInfoGet();
    VIDEO_CONFIG V = BootInfoToVideoConfig(Info);

    FontInit();
    ThemeInit();
    HalVideoSet(&V);
    HalVideoInitBackbuffer();
    HalVideoClearScreen(ThemeDesktopBg());
    HalVideoPresent();
    return 0;
}

static int InitCpu(void) {
    if (HalInit() != 0) {
        return -1;
    }
    HalTimerInit();
    HalSyscallInit();
    /* PR-V3：virtio-input；失败可无头继续（仍有串口） */
    (void)HalUsbInit();
    /* PR-N9：仅 virt 探测 virtio-net（x86 仍由 net 模块 InitNetModule） */
    if (HalPlatformVirtConsole()) {
        (void)HalNetInit();
    }
    return 0;
}

static int InitSmp(void) {
    return HalSmpStartAps();
}

static int InitUsb(void) {
    return HalUsbInit();
}

static int InitFileSystemModule(void) {
    return FileSystemInit();
}

static int InitGuiModule(void) {
    (void)DbInit();
    (void)ThemeLoad();
    LocaleInit();
    GuiInit();
    return 0;
}

static int InitNetModule(void) {
    if (HalNetInit() != 0) {
        return -1;
    }
    UdpInit();
    TcpInit();
    return 0;
}

static int InitSched(void) {
    SchedulerInit();
    return 0;
}

static int InitConsole(void) {
    LocaleInit();
    ConsoleRegisterBuiltins();
    if (HalPlatformVirtConsole() && !gVirtDesktop) {
        ShellCommandsRegisterVirtMin();
    } else {
        ShellCommandsRegister();
    }
    ConsoleInit();
    return 0;
}

/* x86 全量桌面路径 */
static const MODULE gModulesFull[] = {
    { "serial",  InitSerial },
    { "mem",     InitMem },
    { "vmm",     InitVmm },
    { "video",   InitVideo },
    { "cpu",     InitCpu },
    { "smp",     InitSmp },
    { "fs",      InitFileSystemModule },
    { "usb",     InitUsb },
    { "net",     InitNetModule },
    { "gui",     InitGuiModule },
    { "sched",   InitSched },
    { "console", InitConsole },
};

/* PR-A8：virt 串口子集（无 FB / 无盘桌面） */
static const MODULE gModulesVirt[] = {
    { "serial",  InitSerial },
    { "mem",     InitMem },
    { "vmm",     InitVmm },
    { "cpu",     InitCpu },
    { "sched",   InitSched },
    { "console", InitConsole },
};

/* PR-V5：virt 桌面（跳过 smp/net；输入在 InitCpu） */
static const MODULE gModulesVirtDesktop[] = {
    { "serial",  InitSerial },
    { "mem",     InitMem },
    { "vmm",     InitVmm },
    { "video",   InitVideo },
    { "cpu",     InitCpu },
    { "fs",      InitFileSystemModule },
    { "gui",     InitGuiModule },
    { "sched",   InitSched },
    { "console", InitConsole },
};

int KernelModulesVirtDesktop(void) {
    return gVirtDesktop;
}

int KernelModulesRun(void) {
    const BOOT_INFO *Info;

    if (HalPlatformVirtConsole()) {
        Info = BootInfoGet();
        gVirtDesktop = (Info && Info->FrameBufferSize != 0) ? 1 : 0;
        if (gVirtDesktop) {
            return ModulesRun(gModulesVirtDesktop,
                              (int)(sizeof(gModulesVirtDesktop) /
                                    sizeof(gModulesVirtDesktop[0])));
        }
        return ModulesRun(gModulesVirt,
                          (int)(sizeof(gModulesVirt) / sizeof(gModulesVirt[0])));
    }
    gVirtDesktop = 0;
    return ModulesRun(gModulesFull,
                      (int)(sizeof(gModulesFull) / sizeof(gModulesFull[0])));
}
