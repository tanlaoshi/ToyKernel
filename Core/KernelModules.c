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
#include "HalDevices.h"
#include "Device.h"
#include "ToySerialLog.h"

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
    HalSerialInitialize();
    return 0;
}

static int InitializePhysicalMemory(void) {
    return PhysicalMemoryInitialize();
}

static int InitializeVirtualMemory(void) {
    const BOOT_INFO *Info = BootInfoGet();

    if (VirtualMemoryInitialize() != 0) {
        return -1;
    }
    if (Info && Info->FrameBufferBase != 0) {
        UINT64 FbBytes = Info->FrameBufferSize;
        UINT64 Layout = 0;

        if (Info->PixelsPerScanLine != 0 && Info->VerticalResolution != 0) {
            Layout = (UINT64)Info->PixelsPerScanLine *
                     (UINT64)Info->VerticalResolution * 4ull;
        }
        if (Layout > FbBytes) {
            FbBytes = Layout;
        }
        if (FbBytes != 0) {
            VirtualMemoryMapIdentity(Info->FrameBufferBase, FbBytes);
        }
    }
    HalPlatformMapMmio();
    VirtualMemoryEnable();
    return 0;
}

static int InitializeVideo(void) {
    const BOOT_INFO *Info = BootInfoGet();
    VIDEO_CONFIG V = BootInfoToVideoConfig(Info);

    FontInitialize();
    ThemeInitialize();
    HalVideoSet(&V);
    /* PR-G-fb-wc：PAT PA1=WC，仅 LFB 映成 PWT（xHCI 仍 PTE_MMIO/UC） */
    HalVideoEnableFbWc();
    HalVideoInitBackbuffer();
    /*
     * PR-K-log-cont：勿再 ClearScreen / 顶带 Fill——接 Boot+KernelMain 已滚的黑底。
     * 桌面底色由 gui 进桌面时再画。GopEnable 幂等，不重置上滚位置。
     */
    HalSerialGopEnable();
    /* PR-G-fb-pte：映后核验；期望 cache=WC */
    HalVideoLogFbPte();
    return 0;
}

static int InitializeCpu(void) {
    if (HalInit() != 0) {
        return -1;
    }
    HalTimerInitialize();
    HalSyscallInit();
    /* virt：仍在此挂 virtio-input；x86 真机在 file-system 前的 usb 模块（PR-H-msc-7a） */
    if (HalPlatformIsVirtSerialConsole()) {
        (void)HalUsbInit();
    }
    return 0;
}

static int InitializeSmp(void) {
    return HalSmpStartApplicationProcessors();
}

static int InitializeUsb(void) {
    ToyLogBoot("Boot: Input Probe (USB Then PS/2)\n");
    (void)HalUsbInit();
    if (ToyDriverInputReady()) {
        ToyLogBoot("Boot: Input Backend Ready\n");
    } else {
        ToyLogBoot("Boot: Input NONE (Continue)\n");
    }
    /*
     * 真机：Arm 试 irq=msi (dual)，不通则 irq=poll；Drain 始终盲排空。
     * 进 gui 前：对齐鼠坐标并抽空队列/残留键，避免钉死光标或吞首键。
     */
    if (!HalCpuIsHypervisor()) {
        HalInputArmIrq();
        HalVideoLogFbPte();
        {
            UINT32 Cx = 512;
            UINT32 Cy = 384;
            UINT32 Sw = 0;
            UINT32 Sh = 0;

            HalVideoGetSize(&Sw, &Sh);
            if (Sw > 0) {
                Cx = Sw / 2;
            }
            if (Sh > 0) {
                Cy = Sh / 2;
            }
            HalInputMouseHandoffDesktop(Cx, Cy);
        }
        {
            int n;
            HAL_KEYBOARD_REPORT Dump;
            HAL_MOUSE_REPORT Mdump;

            for (n = 0; n < 8; n++) {
                HalInputPoll();
            }
            while (HalKeyboardDequeue(&Dump)) {
            }
            while (HalMouseDequeue(&Mdump)) {
            }
        }
    }
    return 0; /* 无键盘也必须进 gui / 桌面 */
}

static int InitializeFileSystem(void) {
    return FileSystemInitialize();
}

static int InitializeGui(void) {
    if (!HalCpuIsHypervisor()) {
        HalInputPoll();
    }
    (void)DbInit();
    (void)FontLoadAssets(); /* PR-T3：须在 ThemeLoad 前，便于 font= 选中运行时 id */
    if (!HalCpuIsHypervisor()) {
        HalInputPoll();
    }
    (void)ThemeLoad();
    LocaleInitialize();
    if (!HalCpuIsHypervisor()) {
        HalInputPoll();
    }
    /* ThemeLoad 后的 scale=：重配逻辑分辨率后再 GuiInit */
    if (ThemeUiScale() != 100) {
        (void)HalVideoSetUiScale(ThemeUiScale());
    }
    GuiInit();
    if (!HalCpuIsHypervisor()) {
        HalInputPoll();
    }
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
     * 真机无经典 8042 时 STATUS 常浮空 0xFF（OBF 永真）→ 排空 while 死循环，屏停 [Mod] driver。
     * Input / Net 仍由后续 usb / network 模块 Probe。
     */
    DeviceInitialize();
    DeviceEnumerateAll();
    ToyLogBoot("Boot: Device Enumerate Done\n");
    HalDriverRegister();
    ToyLogBoot("Boot: Driver Register OK\n");
    (void)ToyDriverProbeClass(TOY_DRIVER_CLASS_BLOCK);
    ToyLogBoot("Boot: Driver Block Probe Done\n");
    return 0;
}

static int InitializeScheduler(void) {
    SchedulerInitialize();
    return 0;
}

static int InitializeConsole(void) {
    LocaleInitialize();
    ConsoleRegisterBuiltins();
    if (HalConsoleOnly()) {
        ShellCommandsRegisterVirtMin();
    } else {
        ShellCommandsRegister();
    }
    ConsoleUserAliasLoad();
    ConsoleInitialize();
    return 0;
}

/* x86 全量：PR-H-msc-7a — usb（xHCI/HID）在 file-system 之前，供 7b FS 前 auto */
static const MODULE gModulesFull[] = {
    { "Serial",  InitializeSerial },
    { "Memory",     InitializePhysicalMemory },
    { "Driver",     InitializeDriver },
    { "VirtualMemory",     InitializeVirtualMemory },
    { "Video",   InitializeVideo },
    { "Cpu",     InitializeCpu },
    { "Smp",     InitializeSmp },
    { "Usb",     InitializeUsb },
    { "FileSystem",      InitializeFileSystem },
    { "Network",     InitializeNetwork },
    { "Gui",     InitializeGui },
    { "Scheduler",   InitializeScheduler },
    { "Console", InitializeConsole },
};

/* PR-A8 / B1：HalConsoleOnly — 串口子集（无 FB / 命令行靶） */
static const MODULE gModulesVirt[] = {
    { "Serial",  InitializeSerial },
    { "Memory",     InitializePhysicalMemory },
    { "Driver",     InitializeDriver },
    { "VirtualMemory",     InitializeVirtualMemory },
    { "Cpu",     InitializeCpu },
    { "Smp",     InitializeSmp },
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
