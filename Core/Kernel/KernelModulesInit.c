/*
 * KernelModulesInit.c — 子系统 Initialize*（PR-S3-kernelmodules-1）
 *
 * 从 KernelModules.c 原样搬家；不改语义。表与 Run 见 KernelModules.c。
 */
#include "KernelModulesPrivate.h"
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
#include "Font.h"
#include "Theme.h"
#include "Db.h"
#include "Locale.h"
#include "Driver.h"
#include "DriverInput.h"
#include "HalDevices.h"
#include "Device.h"
#include "ToySerialLog.h"

static int VirtualMemoryMapIdentity(UINT64 Phys, UINT64 Size) {
    if (Size == 0) {
        return 0;
    }
    UINT64 Start = Phys & ~(UINT64)(PAGE_SIZE - 1);
    UINT64 End = Phys + Size;
    while (Start < End) {
        if (VirtualMemoryMapPage(Start, Start, PTE_PRESENT | PTE_WRITABLE) != 0) {
            return -1;
        }
        Start += PAGE_SIZE;
    }
    return 0;
}

int InitializeSerial(void) {
    HalSerialInitialize();
    HalSerialEnableRxIrq();
    return 0;
}

int InitializePhysicalMemory(void) {
    return PhysicalMemoryInitialize();
}

int InitializeVirtualMemory(void) {
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
            if (VirtualMemoryMapIdentity(Info->FrameBufferBase, FbBytes) != 0) {
                return -1;
            }
        }
    }
    HalPlatformMapMmio();
    VirtualMemoryEnable();
    return 0;
}

int InitializeVideo(void) {
    const BOOT_INFO *Info = BootInfoGet();
    VIDEO_CONFIG V = BootInfoToVideoConfig(Info);

    FontInitialize();
    ThemeInitialize();
    HalVideoSet(&V);
    /* PR-G-fb-wc：PAT PA1=WC，仅 LFB 映成 PWT（xHCI 仍 PTE_MMIO/UC） */
    HalVideoEnableFbWc();
    HalVideoInitializeBackbuffer();
    /*
     * PR-K-log-cont：勿再 ClearScreen / 顶带 Fill——接 Boot+KernelMain 已滚的黑底。
     * 桌面底色由 gui 进桌面时再画。GopEnable 幂等，不重置上滚位置。
     */
    HalSerialGopEnable();
    /* PR-G-fb-pte：映后核验；期望 cache=WC */
    HalVideoLogFbPte();
    /* PR-G-igpu-1：VMM 已开；核显 BAR 只读指纹（无卡/失败软退） */
    HalIgpuMmioInitialize();
    /* PR-G-audio-1：HDA BAR 只读指纹（无卡/失败软退） */
    HalHdaMmioInitialize();
    /* PR-G-audio-2：CORB/RIRB + codec/pin 枚举（无卡/失败软退） */
    HalHdaCodecInitialize();
    /* PR-G-igpu-3：forcewake 须在显示侧 AUD / Stream 之前 */
    HalIgpuForcewakeInitialize();
    /* PR-G-audio-3：Stream 短 PCM（DP；须 igpu AUD 使能） */
    HalHdaStreamInitialize();
    HalIgpuGttInitialize();
    HalIgpuBlitInitialize();
    HalIgpuPresentPrepare();
    return 0;
}

int InitializeCpu(void) {
    if (HalInitialize() != 0) {
        return -1;
    }
    HalTimerInitialize();
    HalSyscallInitialize();
    /* virt：仍在此挂 virtio-input；x86 真机在 file-system 前的 usb 模块（PR-H-msc-7a） */
    if (HalPlatformIsVirtSerialConsole()) {
        (void)HalUsbInitialize();
    }
    return 0;
}

/*
 * Cpu 后 / Smp 前：只再探 COM1。
 * 勿在此 HalUsbInitialize/UartClaim：FTDI 扫口会 Force 复位尚未认领的 hub 根口，
 * NUC 上随之 MSC bulk 全超时，DesktopInitialize 读壁纸/图标时像「不进桌面」。
 * USB-UART 仍在 FS 认盘后再 claim（见 FileSystemInit）。
 */
int InitializeSerialEarly(void) {
    ToyLogBoot("Boot: Serial Early (COM1)\n");
    HalSerialRetryIfMissing();
    HalSerialEnableRxIrq();
    if (HalSerialPresent()) {
        ToyLogBoot("Boot: COM1 Early Ready\n");
    } else {
        ToyLogBoot("Boot: COM1 Early Miss\n");
    }
    return 0;
}

int InitializeSmp(void) {
    return HalSmpStartApplicationProcessors();
}

int InitializeUsb(void) {
    ToyLogBoot("Boot: Input Probe (USB Then PS/2)\n");
    (void)HalUsbInitialize();
    /* USB-UART 挪到 FS MSC 认盘后，避免 Force 扫口打坏 hub */
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
        /* FB-PTE 已在 Video 模块打过，此处勿再 Log（串口/屏会重复一行） */
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

int InitializeFileSystem(void) {
    return FileSystemInitialize();
}

int InitializeGui(void) {
    if (!HalCpuIsHypervisor()) {
        HalInputPoll();
    }
    (void)DbInitialize();
    (void)FontLoadAssets(); /* PR-T3：须在 ThemeLoad 前，便于 font= 选中运行时 id */
    if (!HalCpuIsHypervisor()) {
        HalInputPoll();
    }
    (void)ThemeLoad();
    LocaleInitialize();
    if (!HalCpuIsHypervisor()) {
        HalInputPoll();
    }
    /* ThemeLoad 后的 scale=：重配逻辑分辨率后再 GuiInitialize */
    if (ThemeUiScale() != 100) {
        (void)HalVideoSetUiScale(ThemeUiScale());
    }
    GuiInitialize(); /* 内已 Deferred 补鼠 + Handoff；此处只抽空残留 */
    if (!HalCpuIsHypervisor()) {
        HAL_MOUSE_REPORT Mdump;

        HalInputPoll();
        while (HalMouseDequeue(&Mdump)) {
        }
    }
    return 0;
}

int InitializeNetwork(void) {
    if (HalNetInitialize() != 0) {
        return -1;
    }
    UdpInitialize();
    TcpInitialize();
    return 0;
}

int InitializeDriver(void) {
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

int InitializeScheduler(void) {
    SchedulerInitialize();
    return 0;
}

int InitializeConsole(void) {
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
