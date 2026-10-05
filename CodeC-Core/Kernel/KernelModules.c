/*
 * KernelModules.c — 三张模块表，各一个 Run（PR-K-seq-2）
 *
 * 人话：串口子集 / virt 桌面 / x86 全量 各走自己的顺序表，这里不再 if 选表。
 * 从哪读：文件下方三张 gModules*，再看对应 KernelModulesRun*。
 * 别改：各表条目顺序（尤其 Full 里 USB 在 FileSystem 前）。
 * Initialize* 见 KernelModulesInit.c。选路见 KernelMain。
 */
#include "KernelModules.h"
#include "KernelModulesPrivate.h"
#include "Module.h"
#include "Hal.h"

static int gVirtDesktop;

#define MODULE_COUNT(Table) ((int)(sizeof(Table) / sizeof((Table)[0])))

/* x86 全量：usb（xHCI/HID）在 file-system 之前，供 FS 前 auto */
static const MODULE gModulesFull[] = {
    { "Serial",        InitializeSerial },
    { "Memory",        InitializePhysicalMemory },
    { "Driver",        InitializeDriver },
    { "VirtualMemory", InitializeVirtualMemory },
    { "Video",         InitializeVideo },
    { "Cpu",           InitializeCpu },
    { "SerialEarly",   InitializeSerialEarly },
    { "Smp",           InitializeSmp },
    { "USB",           InitializeUsb },
    { "FileSystem",    InitializeFileSystem },
    { "Network",       InitializeNetwork },
    { "Gui",           InitializeGui },
    { "Scheduler",     InitializeScheduler },
    { "Console",       InitializeConsole },
};

/* 串口子集。仍挂 FileSystem：virt --serial 有 virtio-blk，供 exec HELLO.ELF */
static const MODULE gModulesVirt[] = {
    { "Serial",        InitializeSerial },
    { "Memory",        InitializePhysicalMemory },
    { "Driver",        InitializeDriver },
    { "VirtualMemory", InitializeVirtualMemory },
    { "Cpu",           InitializeCpu },
    { "Smp",           InitializeSmp },
    { "FileSystem",    InitializeFileSystem },
    { "Scheduler",     InitializeScheduler },
    { "Console",       InitializeConsole },
};

/* virt 桌面（输入在后续路径；挂 net） */
static const MODULE gModulesVirtDesktop[] = {
    { "Serial",        InitializeSerial },
    { "Memory",        InitializePhysicalMemory },
    { "Driver",        InitializeDriver },
    { "VirtualMemory", InitializeVirtualMemory },
    { "Video",         InitializeVideo },
    { "Cpu",           InitializeCpu },
    { "Smp",           InitializeSmp },
    { "FileSystem",    InitializeFileSystem },
    { "Network",       InitializeNetwork },
    { "Gui",           InitializeGui },
    { "Scheduler",     InitializeScheduler },
    { "Console",       InitializeConsole },
};

int KernelModulesVirtDesktop(void) {
    return gVirtDesktop;
}

int KernelModulesRunVirt(void) {
    gVirtDesktop = 0;
    return ModulesRun(gModulesVirt, MODULE_COUNT(gModulesVirt));
}

int KernelModulesRunVirtDesktop(void) {
    gVirtDesktop = 1;
    return ModulesRun(gModulesVirtDesktop, MODULE_COUNT(gModulesVirtDesktop));
}

int KernelModulesRunFull(void) {
    gVirtDesktop = HalHasFrameBuffer() ? 1 : 0;
    return ModulesRun(gModulesFull, MODULE_COUNT(gModulesFull));
}
