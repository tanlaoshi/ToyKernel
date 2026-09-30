/*
 * KernelModules.c — 子系统模块表与 Run（PR-S3-kernelmodules-1）
 *
 * Initialize* 见 KernelModulesInit.c。
 */
#include "KernelModules.h"
#include "KernelModulesPrivate.h"
#include "Module.h"
#include "Hal.h"

static int gVirtDesktop; /* PR-V5/B1：已选桌面模块表（有 FB 且非 ConsoleOnly） */

/* x86 全量：PR-H-msc-7a — usb（xHCI/HID）在 file-system 之前，供 7b FS 前 auto */
static const MODULE gModulesFull[] = {
    { "Serial",  InitializeSerial },
    { "Memory",     InitializePhysicalMemory },
    { "Driver",     InitializeDriver },
    { "VirtualMemory",     InitializeVirtualMemory },
    { "Video",   InitializeVideo },
    { "Cpu",     InitializeCpu },
    { "SerialEarly", InitializeSerialEarly }, /* 仅 COM1；USB-UART 见 FS 后 */
    { "Smp",     InitializeSmp },
    { "Usb",     InitializeUsb },
    { "FileSystem",      InitializeFileSystem },
    { "Network",     InitializeNetwork },
    { "Gui",     InitializeGui },
    { "Scheduler",   InitializeScheduler },
    { "Console", InitializeConsole },
};

/* PR-A8 / B1：HalConsoleOnly — 串口子集（无 FB / 命令行靶）
 * 仍挂 FileSystem：virt --serial 有 virtio-blk，供 PR-A12 exec HELLO.ELF */
static const MODULE gModulesVirt[] = {
    { "Serial",  InitializeSerial },
    { "Memory",     InitializePhysicalMemory },
    { "Driver",     InitializeDriver },
    { "VirtualMemory",     InitializeVirtualMemory },
    { "Cpu",     InitializeCpu },
    { "Smp",     InitializeSmp },
    { "FileSystem",      InitializeFileSystem },
    { "Scheduler",   InitializeScheduler },
    { "Console", InitializeConsole },
};

/* PR-V5/N10/A14：virt 桌面（输入在 usb；N10 挂 net） */
static const MODULE gModulesVirtDesktop[] = {
    { "Serial",  InitializeSerial },
    { "Memory",     InitializePhysicalMemory },
    { "Driver",     InitializeDriver },
    { "VirtualMemory",     InitializeVirtualMemory },
    { "Video",   InitializeVideo },
    { "Cpu",     InitializeCpu },
    { "Smp",     InitializeSmp },
    { "FileSystem",      InitializeFileSystem },
    { "Network",     InitializeNetwork },
    { "Gui",     InitializeGui },
    { "Scheduler",   InitializeScheduler },
    { "Console", InitializeConsole },
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
    if (HalHasFrameBuffer() && HalPlatformIsVirtSerialConsole()) {
        gVirtDesktop = 1;
        return ModulesRun(gModulesVirtDesktop,
                          (int)(sizeof(gModulesVirtDesktop) /
                                sizeof(gModulesVirtDesktop[0])));
    }
    gVirtDesktop = HalHasFrameBuffer() ? 1 : 0;
    return ModulesRun(gModulesFull,
                      (int)(sizeof(gModulesFull) / sizeof(gModulesFull[0])));
}
