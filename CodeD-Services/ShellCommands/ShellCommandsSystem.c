/*
 * ShellCommandsSystem.c — PR-S3-shellsys-1：info/mem/reboot/halt/lsdev + 注册汇总
 *
 * exec/ps/kill/set priority 见 ShellCommandsSystemProc.c。
 */
#include "ShellPrivate.h"
#include "BootInfo.h"
#include "Console.h"
#include "Device.h"
#include "Driver.h"
#include "PhysicalMemory.h"
#include "Hal.h"

static void CommandInfo(int Argc, char **Argv) {
    const BOOT_INFO *Info = BootInfoGet();
    (void)Argc;
    (void)Argv;
    if (!Info) {
        ConsoleWrite("video (no boot info)\n");
        return;
    }
    ConsoleWrite("video ");
    ConsoleWriteHex32(Info->HorizontalResolution);
    ConsoleWrite(" x ");
    ConsoleWriteHex32(Info->VerticalResolution);
    ConsoleWrite(" fb=");
    ConsoleWriteHex64(Info->FrameBufferBase);
    ConsoleWrite("\n");
}

static void CommandMemory(int Argc, char **Argv) {
    const BOOT_INFO *Info = BootInfoGet();
    UINT64 RegionBytes = 0;
    UINT32 i;

    (void)Argc;
    (void)Argv;
    ConsoleWrite("physical memory\n  free  ");
    ConsoleWriteHex64(PhysicalMemoryFreePageCount() << PAGE_SHIFT);
    ConsoleWrite(" bytes (");
    ConsoleWriteHex32((UINT32)PhysicalMemoryFreePageCount());
    ConsoleWrite(" pages)\n  total ");
    ConsoleWriteHex64(PhysicalMemoryTotalPages() << PAGE_SHIFT);
    ConsoleWrite(" bytes tracked\n");
    if (Info) {
        for (i = 0; i < Info->RegionCount; i++) {
            RegionBytes += Info->Regions[i].Size;
        }
        ConsoleWrite("  boot  ");
        ConsoleWriteHex64(RegionBytes);
        ConsoleWrite(" bytes in ");
        ConsoleWriteHex32(Info->RegionCount);
        ConsoleWrite(" region(s)\n");
    }
}

static void CommandMemtest(int Argc, char **Argv) {
    (void)Argc;
    (void)Argv;
    UINT64 Before = PhysicalMemoryFreePageCount();
    void *Page = PhysicalMemoryAllocatePage();
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
            ConsoleWriteHex32((UINT32)i);
            ConsoleWrite("\n");
            PhysicalMemoryFreePage(Page);
            return;
        }
    }
    ConsoleWrite("memtest: page ");
    ConsoleWriteHex64((UINT64)(UINTN)Page);
    ConsoleWrite(" ok, freeing\n");
    PhysicalMemoryFreePage(Page);
    ConsoleWrite("memtest: free pages ");
    ConsoleWriteHex32((UINT32)Before);
    ConsoleWrite(" -> ");
    ConsoleWriteHex32((UINT32)PhysicalMemoryFreePageCount());
    ConsoleWrite("\n");
}

static void CommandReboot(int Argc, char **Argv) {
    (void)Argc;
    (void)Argv;
    ConsoleWrite("rebooting...\n");
    HalConsoleWriteSerial("shell: reboot\n");
    HalCpuReboot();
}

static void CommandHalt(int Argc, char **Argv) {
    (void)Argc;
    (void)Argv;
    ConsoleWrite("halt\n");
    HalCpuPark();
}

/* PR-D4：列出已绑定驱动（TOY_DRIVER.Name + 类） */
static const char *DriverClassName(TOY_DRIVER_CLASS Class) {
    switch (Class) {
    case TOY_DRIVER_CLASS_BLOCK:
        return "block";
    case TOY_DRIVER_CLASS_INPUT:
        return "input";
    case TOY_DRIVER_CLASS_NET:
        return "net";
    case TOY_DRIVER_CLASS_DISPLAY:
        return "display";
    case TOY_DRIVER_CLASS_AUDIO:
        return "audio";
    default:
        return "?";
    }
}

static void CommandLsdev(int Argc, char **Argv) {
    UINTN i;
    UINTN Bound = 0;

    if (DeviceListDumpArgs(Argc, Argv) != 0) {
        return;
    }

    for (i = 0; i < ToyDriverInstanceCount(); i++) {
        const TOY_DRIVER_INSTANCE *Inst = ToyDriverInstanceGet(i);
        if (!Inst || !Inst->Bound || !Inst->Driver || !Inst->Driver->Name) {
            continue;
        }
        Bound++;
    }
    ConsoleWrite("\n=== Bound Drivers (");
    ConsoleWriteHex32((UINT32)Bound);
    ConsoleWrite(") ===\n");
    if (Bound == 0) {
        ConsoleWrite("  (none)\n");
        return;
    }
    for (i = 0; i < ToyDriverInstanceCount(); i++) {
        const TOY_DRIVER_INSTANCE *Inst = ToyDriverInstanceGet(i);
        if (!Inst || !Inst->Bound || !Inst->Driver || !Inst->Driver->Name) {
            continue;
        }
        ConsoleWrite("  ");
        ConsoleWrite(Inst->Driver->Name);
        ConsoleWrite("  ");
        ConsoleWrite(DriverClassName(Inst->Driver->Class));
        /* PR-H4e-2：net + e1000* 时附链路 */
        if (Inst->Driver->Class == TOY_DRIVER_CLASS_NET) {
            int Up = 0;
            UINT32 Mbps = 0;
            int Fd = 0;

            if (HalNetGetLinkInfo(&Up, &Mbps, &Fd)) {
                ConsoleWrite("  link=");
                ConsoleWrite(Up ? "up" : "down");
                if (Up) {
                    if (Mbps == 1000u) {
                        ConsoleWrite(" 1000");
                    } else if (Mbps == 100u) {
                        ConsoleWrite(" 100");
                    } else if (Mbps == 10u) {
                        ConsoleWrite(" 10");
                    }
                    ConsoleWrite(Fd ? "/FD" : "/HD");
                }
            }
        }
        ConsoleWrite("\n");
    }
}

void ShellCommandsSystemRegisterVirtMin(void) {
    ShellCommandsSystemProcRegisterVirtMin();
    ConsoleRegister2("show", "memory", "physical memory stats", CommandMemory);
    ConsoleRegisterAliasLine("mem", "show", "memory");
    ConsoleRegister2("list", "devices", "list devices + bound drivers", CommandLsdev);
    ConsoleRegisterAliasLine("lsdev", "list", "devices");
    ConsoleRegister("halt", "stop CPU", CommandHalt);
    ConsoleRegisterAlias("halt", "exit");
    ConsoleRegisterAlias("halt", "quit");
}

void ShellCommandsSystemRegister(void) {
    ShellCommandsSystemProcRegister();

    ConsoleRegister2("list", "devices", "list devices + bound drivers", CommandLsdev);
    ConsoleRegisterAliasLine("lsdev", "list", "devices");

    ConsoleRegister2("show", "memory", "physical memory stats", CommandMemory);
    ConsoleRegister2("show", "info", "boot framebuffer info", CommandInfo);
    ConsoleRegisterAliasLine("mem", "show", "memory");
    ConsoleRegisterAliasLine("memory", "show", "memory");
    ConsoleRegisterAliasLine("info", "show", "info");

    ConsoleRegister2("test", "memory", "alloc/verify/free one page", CommandMemtest);
    ConsoleRegisterAliasLine("memtest", "test", "memory");

    ConsoleRegister("reboot", "reset CPU (QEMU display: quit+./run-split.sh)", CommandReboot);
    ConsoleRegister("halt", "stop CPU", CommandHalt);
    ConsoleRegisterAlias("halt", "exit");
    ConsoleRegisterAlias("halt", "quit");
}
