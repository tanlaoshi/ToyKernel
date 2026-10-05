/*
 * KernelModulesInit.c — 其余 Initialize*（PR-K-seq-4）
 *
 * Video / USB / Gui 见同目录 KernelModulesInitVideo.c 等。表与 Run 见 KernelModules.c。
 */
#include "KernelModulesPrivate.h"
#include "BootInfo.h"
#include "Hal.h"
#include "Console.h"
#include "FileSystem.h"
#include "Scheduler.h"
#include "PhysicalMemory.h"
#include "VirtualMemory.h"
#include "Udp.h"
#include "Tcp.h"
#include "ShellCommands.h"
#include "Locale.h"
#include "Driver.h"
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
    if (HalFpuSelfTest() == 0) {
        ToyLogBoot("Boot: fpu island ok\n");
    } else {
        ToyLogBoot("Boot: fpu island skip\n");
    }
    return 0;
}

int InitializeSmp(void) {
    return HalSmpStartApplicationProcessors();
}

int InitializeFileSystem(void) {
    return FileSystemInitialize();
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
