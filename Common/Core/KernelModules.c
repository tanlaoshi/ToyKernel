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
#include "DriverInput.h"

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
    UINT32 W;
    UINT32 H;

    FontInit();
    ThemeInit();
    HalVideoSet(&V);
    HalVideoInitBackbuffer();
    HalVideoClearScreen(ThemeDesktopBackground());
    /* 再清一遍顶带，去掉固件/进度条残留色块 */
    HalVideoGetSize(&W, &H);
    if (W > 0) {
        HalVideoFillRect(0, 0, W, 64, ThemeDesktopBackground());
    }
    HalVideoPresent();
    HalSerialGopEnable();
    return 0;
}

static int InitializeCpu(void) {
    if (HalInit() != 0) {
        return -1;
    }
    HalTimerInit();
    HalSyscallInit();
    /* virt：仍在此挂 virtio-input；x86 真机延后到 gui 后的 usb 模块（与 main 一致） */
    if (HalPlatformVirtConsole()) {
        (void)HalUsbInit();
    }
    return 0;
}

static int InitializeSmp(void) {
    return HalSmpStartApplicationProcessors();
}

static int InitializeUsb(void) {
    HalSerialWrite("boot: input probe (USB then PS/2)\n");
    (void)HalUsbInit();
    if (ToyDriverInputReady()) {
        HalSerialWrite("boot: input backend ready\n");
    } else {
        HalSerialWrite("boot: input NONE (continue)\n");
    }
    /*
     * 真机：Arm 保持 irq=poll (base)（XhciEnableIrq 零 MSI + dual stub），
     * 再 PHOTO 拍尾部日志。QEMU 不走 PhotoHold。
     */
    if (!HalCpuIsHypervisor()) {
        HalSerialWrite("boot: xhci-Hhid photo-hold build\n");
        HalInputArmIrq();
        HalSerialGopPhotoHold(20);
    }
    return 0; /* 无键盘也必须进 gui / 桌面 */
}

static int InitializeFileSystem(void) {
    return FileSystemInit();
}

static int InitializeGui(void) {
    HalSerialWrite("boot: gui...\n");
    (void)DbInit();
    HalSerialWrite("boot: gui fonts\n");
    (void)FontLoadAssets();
    HalSerialWrite("boot: gui theme\n");
    (void)ThemeLoad();
    LocaleInit();
    HalSerialWrite("boot: gui DesktopInit\n");
    GuiInit();
    HalSerialWrite("boot: gui ready\n");
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
    /*
     * PR-D2：只早 Probe Block（ATA PIO 无需 MMIO）。
     * 勿 ProbeAll：VMM 前 xHCI/AHCI/NVMe/Net 本会跳过，但 ps2-kbd 会跑 Ps2InitHw；
     * 真机无经典 8042 时 STATUS 常浮空 0xFF（OBF 永真）→ 排空 while 死循环，屏停 [mod] driver。
     * Input / Net 仍由后续 usb / network 模块 Probe。
     */
    HalDriverRegister();
    HalSerialWrite("boot: driver register ok\n");
    (void)ToyDriverProbeClass(TOY_DRIVER_CLASS_BLOCK);
    HalSerialWrite("boot: driver block probe done\n");
    return 0;
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

/* x86 全量：usb 在 gui 前，便于桌面叠画探测结果（与 main 一致） */
static const MODULE gModulesFull[] = {
    { "serial",  InitializeSerial },
    { "memory",     InitializePhysicalMemory },
    { "driver",     InitializeDriver },
    { "virtual-memory",     InitializeVirtualMemory },
    { "video",   InitializeVideo },
    { "cpu",     InitializeCpu },
    { "smp",     InitializeSmp },
    { "file-system",      InitializeFileSystem },
    { "network",     InitializeNetwork },
    { "usb",     InitializeUsb },
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

/* PR-V5/N10/A14：virt 桌面（输入在 usb；N10 挂 net） */
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
