/*
 * KernelModules.c — 子系统模块初始化表
 */
#include "KernelModules.h"
#include "Module.h"
#include "BootInfo.h"
#include "Hal.h"
#include "Video.h"
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

    HalVideoSet(&V);
    VideoClearScreen(COLOR_DARK_GRAY);
    return 0;
}

static int InitCpu(void) {
    if (HalInit() != 0) {
        return -1;
    }
    HalSyscallInit();
    return 0;
}

static int InitUsb(void) {
    return HalUsbInit();
}

static int InitFileSystemModule(void) {
    return FileSystemInit();
}

static int InitGuiModule(void) {
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
    /* 内置命令优先，避免 fs + ShellCommands 占满命令表 */
    ConsoleRegisterBuiltins();
    ShellCommandsRegister();
    ConsoleInit();
    return 0;
}

static const MODULE gModules[] = {
    { "serial",  InitSerial },
    { "mem",     InitMem },
    { "vmm",     InitVmm },
    { "video",   InitVideo },
    { "cpu",     InitCpu },
    { "fs",      InitFileSystemModule },
    { "usb",     InitUsb },
    { "net",     InitNetModule },
    { "gui",     InitGuiModule },
    { "sched",   InitSched },
    { "console", InitConsole },
};

int KernelModulesRun(void) {
    return ModulesRun(gModules, (int)(sizeof(gModules) / sizeof(gModules[0])));
}
