/*
 * Kernel.c — ToyOS 内核主控
 *
 * 职责：保存启动配置、按模块表初始化子系统、创建 Shell/Worker 任务并启动调度器。
 * Shell 任务处理 USB 键盘输入；Worker 为抢占调度演示用的后台循环。
 */
#include "Kernel.h"
#include "Module.h"
#include "hal.h"
#include "Video.h"
#include "UI.h"
#include "Serial.h"
#include "PCIe.h"
#include "XHCI.h"
#include "HIDKeyboard.h"
#include "Console.h"
#include "Fs.h"
#include "Sched.h"
#include "Pmm.h"
#include "Vmm.h"
#include "Syscall.h"
#include "Process.h"
#include "Gui.h"
#include "Net.h"

static BOOT_CONFIG gBootConfig;
static BOOT_CONFIG *gBootConfigPtr;
static USB_CONTROLLER gXhciDev;
static volatile UINT32 gWorkerCount;

extern char __kernel_end[];

/* Shell 命令 info：打印 GOP 分辨率与帧缓冲物理地址 */
static void CmdInfo(int Argc, char **Argv) {
    (void)Argc;
    (void)Argv;
    ConsoleWrite("video ");
    ConsoleWrite(Uint32ToHex(gBootConfig.VideoConfig.HorizontalResolution));
    ConsoleWrite(" x ");
    ConsoleWrite(Uint32ToHex(gBootConfig.VideoConfig.VerticalResolution));
    ConsoleWrite(" fb=");
    ConsoleWrite(Uint64ToHex(gBootConfig.VideoConfig.FrameBufferBase));
    ConsoleWrite("\n");
}

/* Shell 命令 mem：显示物理内存空闲/总量 */
static void CmdMem(int Argc, char **Argv) {
    (void)Argc;
    (void)Argv;
    ConsoleWrite("physical memory\n  free  ");
    ConsoleHex64(PmmFreePageCount() << PAGE_SHIFT);
    ConsoleWrite(" bytes (");
    ConsoleHex32((UINT32)PmmFreePageCount());
    ConsoleWrite(" pages)\n  total ");
    ConsoleHex64(PmmTotalPages() << PAGE_SHIFT);
    ConsoleWrite(" bytes tracked\n");
}

/* Shell 命令 memtest：分配一页、写读校验、释放并对比空闲页数 */
static void CmdMemtest(int Argc, char **Argv) {
    (void)Argc;
    (void)Argv;
    UINT64 Before = PmmFreePageCount();
    void *Page = PmmAllocPage();
    if (Page == 0) {
        ConsoleWrite("memtest: alloc failed\n");
        return;
    }
    UINT8 *Bytes = (UINT8 *)Page;
    for (int i = 0; i < (int)PAGE_SIZE; i++) {
        Bytes[i] = (UINT8)i;
    }
    for (int i = 0; i < (int)PAGE_SIZE; i++) {
        if (Bytes[i] != (UINT8)i) {
            ConsoleWrite("memtest: verify failed at ");
            ConsoleHex32((UINT32)i);
            ConsoleWrite("\n");
            PmmFreePage(Page);
            return;
        }
    }
    ConsoleWrite("memtest: page ");
    ConsoleHex64((UINT64)(UINTN)Page);
    ConsoleWrite(" ok, freeing\n");
    PmmFreePage(Page);
    ConsoleWrite("memtest: free pages ");
    ConsoleHex32((UINT32)Before);
    ConsoleWrite(" -> ");
    ConsoleHex32((UINT32)PmmFreePageCount());
    ConsoleWrite("\n");
}

/* Shell 命令 runuser：调度内嵌 hello ELF 用户任务 */
static void CmdRunuser(int Argc, char **Argv) {
    (void)Argc;
    (void)Argv;
    UserRunDemo();
}

/* Shell 命令 exec：从 FAT 加载 ELF 并创建用户任务 */
static void CmdExec(int Argc, char **Argv) {
    if (Argc < 2) {
        ConsoleWrite("usage: exec <file>\n");
        return;
    }
    ProcessExec(Argv[1]);
}

/* Shell 命令 ps：列出任务类型、CR3、RIP 与 tick */
static void CmdPs(int Argc, char **Argv) {
    (void)Argc;
    (void)Argv;
    for (int i = 0; i < MAX_TASKS; i++) {
        const TASK *T = SchedTaskByIndex(i);
        if (!T) {
            continue;
        }
        ConsoleWrite("  ");
        ConsoleWrite(T->Name);
        if (T->IsUser) {
            ConsoleWrite(" user");
        } else {
            ConsoleWrite(" kern");
        }
        ConsoleWrite(" cr3=");
        ConsoleHex64(T->Cr3);
        ConsoleWrite(" rip=");
        ConsoleHex64(SchedTaskRip(T));
        ConsoleWrite(" ticks=");
        ConsoleHex32(T->Ticks);
        if (SchedCurrent() == T) {
            ConsoleWrite(" *");
        }
        ConsoleWrite("\n");
    }
    ConsoleWrite("worker loops=");
    ConsoleHex32(gWorkerCount);
    ConsoleWrite("\n");
}

/* Shell 命令 net：显示网卡 MAC 与 IP */
static void CmdNet(int Argc, char **Argv) {
    char IpBuf[20];
    char Hex[3];
    UINT8 Mac[6];
    int i;
    static const char Digits[] = "0123456789ABCDEF";

    (void)Argc;
    (void)Argv;
    if (!NetReady()) {
        ConsoleWrite("net: not available (no virtio-net)\n");
        return;
    }
    NetGetMac(Mac);
    NetFormatIp(NetGetIp(), IpBuf, sizeof(IpBuf));
    ConsoleWrite("mac ");
    for (i = 0; i < 6; i++) {
        Hex[0] = Digits[(Mac[i] >> 4) & 0xF];
        Hex[1] = Digits[Mac[i] & 0xF];
        Hex[2] = 0;
        ConsoleWrite(Hex);
        if (i < 5) {
            ConsoleWrite(":");
        }
    }
    ConsoleWrite("\nip  ");
    ConsoleWrite(IpBuf);
    ConsoleWrite("/24 gw 10.0.2.2 (QEMU user)\n");
}

/* Shell 命令 ping：ICMP echo */
static void CmdPing(int Argc, char **Argv) {
    if (Argc < 2) {
        ConsoleWrite("usage: ping <ip>\n");
        return;
    }
    if (!NetReady()) {
        ConsoleWrite("net: not available\n");
        return;
    }
    ConsoleWrite("ping ");
    ConsoleWrite(Argv[1]);
    ConsoleWrite(" ...\n");
    if (NetPing(Argv[1], 3000) == 0) {
        ConsoleWrite("reply from ");
        ConsoleWrite(Argv[1]);
        ConsoleWrite("\n");
    } else {
        ConsoleWrite("no reply\n");
    }
}

/* 模块初始化：串口 */
static int InitSerial(void) {
    SerialInit();
    return 0;
}

/* 模块初始化：物理内存分配器 PMM */
static int InitMem(void) {
    PMM_BOOT_INFO Info = {
        .Map = &gBootConfig.MemoryMap,
        .KernelStart = 0x100000,
        .KernelEnd = (UINT64)(UINTN)__kernel_end,
        .BootConfigPhys = (UINT64)(UINTN)gBootConfigPtr,
        .FrameBufferBase = gBootConfig.VideoConfig.FrameBufferBase,
        .FrameBufferSize = gBootConfig.VideoConfig.FrameBufferSize,
    };
    return PmmInit(&Info);
}

/* 映射 MMIO / 帧缓冲等高位区域（恒等映射） */
static void VmmMapIdentity(UINT64 Phys, UINT64 Size) {
    if (Size == 0) {
        return;
    }
    UINT64 Start = Phys & ~(UINT64)(PAGE_SIZE - 1);
    UINT64 End = Phys + Size;
    while (Start < End) {
        VmmMapPage(Start, Start, PTE_PRESENT | PTE_WRITABLE);
        Start += PAGE_SIZE;
    }
}

/* 模块初始化：四级页表与恒等映射，启用 CR0.PG */
static int InitVmm(void) {
    if (VmmInit() != 0) {
        return -1;
    }
    if (gBootConfig.VideoConfig.FrameBufferSize != 0) {
        VmmMapIdentity(gBootConfig.VideoConfig.FrameBufferBase,
                       gBootConfig.VideoConfig.FrameBufferSize);
    }
    VmmMapIdentity(0xFEE00000ULL, 0x100000ULL);
    if (gBootConfig.XhciBaseAddress != 0) {
        VmmMapIdentity(gBootConfig.XhciBaseAddress, 0x1000000ULL);
    }
    VmmEnable();
    return 0;
}

/* 模块初始化：帧缓冲与清屏 */
static int InitVideo(void) {
    SetVideo(&gBootConfig.VideoConfig);
    ClearScreen(COLOR_DARK_GRAY);
    return 0;
}

/* 模块初始化：GDT/IDT/LAPIC/中断 + 系统调用门 */
static int InitCpu(void) {
    if (HalInit(&gBootConfig) != 0) {
        return -1;
    }
    HalSyscallInit();
    return 0;
}

/* 模块初始化：PCI 扫描 XHCI、初始化键盘与 MSI-X 中断 */
static int InitUsb(void) {
    USB_CONTROLLER Controllers[8];
    int Count = PciScanUSBControllers(Controllers, 8);
    int Found = 0;
    for (int i = 0; i < Count; i++) {
        if (Controllers[i].Type == 0x30) {
            gXhciDev = Controllers[i];
            Found = 1;
            break;
        }
    }
    if (!Found) {
        if (gBootConfig.XhciBaseAddress == 0) {
            return -1;
        }
        gXhciDev.BaseAddress = gBootConfig.XhciBaseAddress;
        gXhciDev.Bar[0] = gBootConfig.XhciBaseAddress;
        gXhciDev.Type = 0x30;
    }
    if (!XHCIInit64(gXhciDev.BaseAddress)) {
        return -1;
    }
    if (!XHCIEnableIrq(&gXhciDev)) {
        ConsoleWrite("XHCI: IRQ not enabled\n");
        return -1;
    }
    return 0;
}

/* 模块初始化：ATA/GPT/FAT 文件系统 */
static int InitFsModule(void) {
    return FsInit();
}

/* 模块初始化：桌面与窗口 */
static int InitGuiModule(void) {
    GuiInit();
    return 0;
}

/* 模块初始化：virtio-net */
static int InitNetModule(void) {
    return NetInit();
}

/* 模块初始化：调度器数据结构 */
static int InitSched(void) {
    SchedInit();
    return 0;
}

/* 模块初始化：注册 Shell 命令并显示提示符 */
static int InitConsole(void) {
    ConsoleRegister("info", "boot framebuffer info", CmdInfo);
    ConsoleRegister("ps", "list tasks", CmdPs);
    ConsoleRegister("mem", "physical memory stats", CmdMem);
    ConsoleRegister("memtest", "alloc/verify/free one page", CmdMemtest);
    ConsoleRegister("runuser", "run embedded hello ELF", CmdRunuser);
    ConsoleRegister("exec", "load ELF from FAT", CmdExec);
    ConsoleRegister("net", "network info", CmdNet);
    ConsoleRegister("ping", "ICMP echo", CmdPing);
    ConsoleInit();
    return 0;
}

static const MODULE gModules[] = {
    { "serial",  InitSerial },
    { "mem",     InitMem },
    { "vmm",     InitVmm },
    { "video",   InitVideo },
    { "cpu",     InitCpu },
    { "fs",      InitFsModule },
    { "usb",     InitUsb },
    { "net",     InitNetModule },
    { "gui",     InitGuiModule },
    { "sched",   InitSched },
    { "console", InitConsole },
};

/* 将 HID 键盘报告中的新按键事件喂给 Console（边沿检测，避免重复） */
static void FeedHid(USB_KEYBOARD_REPORT *Report, USB_KEYBOARD_REPORT *Previous) {
    for (int i = 0; i < 6; i++) {
        UINT8 Key = Report->KeyCode[i];
        if (Key == 0) {
            continue;
        }
        int WasDown = 0;
        for (int j = 0; j < 6; j++) {
            if (Previous->KeyCode[j] == Key) {
                WasDown = 1;
                break;
            }
        }
        if (WasDown) {
            continue;
        }

        if (Key == HID_KEY_ENTER) {
            ConsoleOnEnter();
            continue;
        }
        if (Key == HID_KEY_LEFT || Key == HID_KEY_RIGHT ||
            Key == HID_KEY_UP || Key == HID_KEY_DOWN) {
            GuiOnArrowKey(Key);
            continue;
        }
        if (Key == HID_KEY_BACKSPACE) {
            ConsoleOnBackspace();
            continue;
        }

        char C = HIDKeyCodeToASCII(Key, Report->ModifierKeys);
        if (C != 0) {
            ConsoleOnChar(C);
        }
    }
}

/* GUI 任务：处理 USB 鼠标/平板移动与点击 */
static void GuiTask(void) {
    for (;;) {
        GuiPollMouse();
        HalCpuHalt();
    }
}

/* Shell 任务：hlt 等待 USB 中断，出队键盘报告并转发给 Console */
static void ShellTask(void) {
    USB_KEYBOARD_REPORT Report = {0};
    USB_KEYBOARD_REPORT Previous = {0};
    ConsoleWrite("shell task running (preemptive)\n");
    for (;;) {
        while (SerialDataReady()) {
            char C = SerialReadChar();
            if (C == '\r' || C == '\n') {
                ConsoleOnEnter();
            } else if (C == '\b' || C == 127) {
                ConsoleOnBackspace();
            } else if (C >= 32 && C <= 126) {
                ConsoleOnChar(C);
            }
        }
        HalCpuHalt();
        while (XHCIDequeueKeyboard(&Report)) {
            FeedHid(&Report, &Previous);
            Previous = Report;
        }
        NetPoll();
        GuiPollMouse();
    }
}

/* Worker 任务：空转计数，演示定时器抢占 */
static void WorkerTask(void) {
    for (;;) {
        gWorkerCount++;
        for (volatile int i = 0; i < 5000; i++) {
        }
        HalCpuHalt();
    }
}

/* 内核 C 入口：由 ToyBoot 在 ExitBootServices 后跳转至此 */
void KernelEntry(BOOT_CONFIG *BootConfig) {
    gBootConfigPtr = BootConfig;
    gBootConfig = *BootConfig;

    if (ModulesRun(gModules, (int)(sizeof(gModules) / sizeof(gModules[0]))) != 0) {
        for (;;) {
            HalCpuPark();
        }
    }

    SchedCreate("shell", ShellTask);
    SchedCreate("gui", GuiTask);
    SchedCreate("worker", WorkerTask);
    SchedStart();
}
