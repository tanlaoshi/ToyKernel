/*
 * ShellCmdSystem.c — PR-S-shell-split-3：info/mem/ps/kill/reboot/exec/halt/lsdev…
 *
 * 从 ShellCommands.c 原样搬家；不改语义。
 */
#include "ShellPriv.h"
#include "BootInfo.h"
#include "Console.h"
#include "Driver.h"
#include "PhysicalMemory.h"
#include "Process.h"
#include "Scheduler.h"
#include "Syscall.h"
#include "Tasks.h"
#include "Hal.h"
#include "VirtualMemory.h"

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

static void CommandRunuser(int Argc, char **Argv) {
    (void)Argc;
    (void)Argv;
    if (ProcessRunDemo() == 0) {
        ConsoleWaitPrompt();
    }
}

static void CommandExec(int Argc, char **Argv) {
    if (Argc < 2) {
        ConsoleWrite("usage: exec <file>\n");
        return;
    }
    if (ProcessExec(Argv[1]) == 0) {
        /* virt：ProcessExec 内已协作跑完并 ShowPrompt；x86 等定时器收尸 */
        if (!HalPlatformIsVirtSerialConsole()) {
            ConsoleWaitPrompt();
        }
    }
}

static void CommandPs(int Argc, char **Argv) {
    (void)Argc;
    (void)Argv;
    for (int i = 0; i < MAX_TASKS; i++) {
        const TASK *T = SchedulerTaskByIndex(i);
        if (!T) {
            continue;
        }
        ConsoleWrite("  pid=");
        ConsoleWriteHex32((UINT32)(i + 1));
        ConsoleWrite(" ");
        ConsoleWrite(T->Name);
        if (T->IsUser) {
            ConsoleWrite(" user");
        } else {
            ConsoleWrite(" kern");
        }
        if (T->State == TASK_ZOMBIE) {
            ConsoleWrite(" zombie");
        } else if (T->State == TASK_BLOCKED) {
            ConsoleWrite(" blocked");
        }
        ConsoleWrite(" root=");
        ConsoleWriteHex64(T->PageRoot);
        ConsoleWrite(" rip=");
        ConsoleWriteHex64(SchedulerTaskRip(T));
        ConsoleWrite(" ticks=");
        ConsoleWriteHex32(T->Ticks);
        ConsoleWrite(" cpu=");
        ConsoleWriteHex32((UINT32)T->OnCpu);
        ConsoleWrite(" home=");
        ConsoleWriteHex32((UINT32)T->HomeCpu);
        ConsoleWrite(" prio=");
        if (T->Priority < 0) {
            ConsoleWrite("-");
            ConsoleWriteHex32((UINT32)(-T->Priority));
        } else {
            ConsoleWriteHex32((UINT32)T->Priority);
        }
        if (SchedulerCurrent() == T) {
            ConsoleWrite(" *");
        }
        ConsoleWrite("\n");
    }
    ConsoleWrite("cpu ticks=");
    ConsoleWriteHex64(HalCpuTicks(0));
    ConsoleWrite(" worker loops=");
    ConsoleWriteHex32(WorkerLoopCount());
    ConsoleWrite(" steals=");
    ConsoleWriteHex64(SchedulerStealCount());
    ConsoleWrite("\n");
}

static int ParseDecInt(const char *S, INT32 *Out) {
    INT32 V = 0;
    int Neg = 0;
    if (!S || !S[0] || !Out) {
        return -1;
    }
    if (*S == '-') {
        Neg = 1;
        S++;
        if (!*S) {
            return -1;
        }
    }
    for (; *S; S++) {
        if (*S < '0' || *S > '9') {
            return -1;
        }
        V = V * 10 + (*S - '0');
    }
    *Out = Neg ? -V : V;
    return 0;
}

/* PR-S-lock：set priority <pid> <prio>；pid 与 list tasks 一致 */
static void CommandSetPriority(int Argc, char **Argv) {
    INT32 Pid = 0;
    INT32 Priority = 0;

    if (Argc < 3) {
        ConsoleWrite("usage: set priority <pid> <prio>\n");
        ConsoleWrite("  prio: -128..127 (higher runs sooner; shell/gui default 8)\n");
        return;
    }
    if (ParseDecInt(Argv[1], &Pid) != 0 || Pid <= 0) {
        ConsoleWrite("set priority: bad pid\n");
        return;
    }
    if (ParseDecInt(Argv[2], &Priority) != 0) {
        ConsoleWrite("set priority: bad prio\n");
        return;
    }
    if (SchedulerSetPriority(Pid, Priority) != 0) {
        ConsoleWrite("set priority: fail\n");
        return;
    }
    ConsoleWrite("set priority: pid=");
    ConsoleWriteHex32((UINT32)Pid);
    ConsoleWrite(" prio=");
    if (Priority < 0) {
        ConsoleWrite("-");
        ConsoleWriteHex32((UINT32)(-Priority));
    } else {
        ConsoleWriteHex32((UINT32)Priority);
    }
    ConsoleWrite("\n");
}

/* PR-P4：kill <pid> [sig]；pid 与 ps / fork 一致（槽位+1）；默认 SIGTERM */
static void CommandKill(int Argc, char **Argv) {
    INT32 Pid = 0;
    INT32 Sig = SIGTERM;

    if (Argc < 2) {
        ConsoleWrite("usage: kill <pid> [sig]\n");
        ConsoleWrite("  sig: 2=INT 9=KILL 15=TERM (default)\n");
        return;
    }
    if (ParseDecInt(Argv[1], &Pid) != 0 || Pid <= 0) {
        ConsoleWrite("kill: bad pid\n");
        return;
    }
    if (Argc >= 3) {
        if (ParseDecInt(Argv[2], &Sig) != 0) {
            ConsoleWrite("kill: bad sig\n");
            return;
        }
    }
    if (SchedulerKillPid(Pid, Sig) != 0) {
        ConsoleWrite("kill: failed (user only; INT/KILL/TERM)\n");
        return;
    }
    ConsoleWrite("kill: ok\n");
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
    default:
        return "?";
    }
}

static void CommandLsdev(int Argc, char **Argv) {
    UINTN i;
    UINTN Bound = 0;
    (void)Argc;
    (void)Argv;

    for (i = 0; i < ToyDriverInstanceCount(); i++) {
        const TOY_DRIVER_INSTANCE *Inst = ToyDriverInstanceGet(i);
        if (!Inst || !Inst->Bound || !Inst->Driver || !Inst->Driver->Name) {
            continue;
        }
        Bound++;
    }
    if (Bound == 0) {
        ConsoleWrite("lsdev: none\n");
        return;
    }
    ConsoleWrite("lsdev: bound=");
    ConsoleWriteHex32((UINT32)Bound);
    ConsoleWrite("\n");
    for (i = 0; i < ToyDriverInstanceCount(); i++) {
        const TOY_DRIVER_INSTANCE *Inst = ToyDriverInstanceGet(i);
        if (!Inst || !Inst->Bound || !Inst->Driver || !Inst->Driver->Name) {
            continue;
        }
        ConsoleWrite("  ");
        ConsoleWrite(Inst->Driver->Name);
        ConsoleWrite("  ");
        ConsoleWrite(DriverClassName(Inst->Driver->Class));
        ConsoleWrite("\n");
    }
}


void ShellCmdSystemRegisterVirtMin(void) {
    ConsoleRegister2("list", "tasks", "list tasks", CommandPs);
    ConsoleRegisterAliasLine("ps", "list", "tasks");
    ConsoleRegister2("show", "memory", "physical memory stats", CommandMemory);
    ConsoleRegisterAliasLine("mem", "show", "memory");
    ConsoleRegister("execute", "load ELF (TOYOS:FILE)", CommandExec);
    ConsoleRegisterAlias("execute", "exec");
    ConsoleRegister("kill", "signal user task (PR-P4)", CommandKill);
    ConsoleRegister2("list", "devices", "list bound drivers", CommandLsdev);
    ConsoleRegisterAliasLine("lsdev", "list", "devices");
    ConsoleRegister("halt", "stop CPU", CommandHalt);
    ConsoleRegisterAlias("halt", "exit");
    ConsoleRegisterAlias("halt", "quit");
}

void ShellCmdSystemRegister(void) {
    ConsoleRegister2("list", "tasks", "list tasks", CommandPs);
    ConsoleRegister2("list", "devices", "list bound drivers", CommandLsdev);
    ConsoleRegisterAliasLine("ps", "list", "tasks");
    ConsoleRegisterAliasLine("tasks", "list", "tasks");
    ConsoleRegisterAliasLine("lsdev", "list", "devices");

    ConsoleRegister2("show", "memory", "physical memory stats", CommandMemory);
    ConsoleRegister2("show", "info", "boot framebuffer info", CommandInfo);
    ConsoleRegisterAliasLine("mem", "show", "memory");
    ConsoleRegisterAliasLine("memory", "show", "memory");
    ConsoleRegisterAliasLine("info", "show", "info");

    ConsoleRegister2("test", "memory", "alloc/verify/free one page", CommandMemtest);
    ConsoleRegisterAliasLine("memtest", "test", "memory");

    ConsoleRegister2("run", "user", "run embedded hello ELF", CommandRunuser);
    ConsoleRegisterAliasLine("runuser", "run", "user");

    ConsoleRegister2("set", "priority", "set priority <pid> <prio>", CommandSetPriority);
    ConsoleRegisterAliasLine("nice", "set", "priority");

    ConsoleRegister("execute", "load ELF (TOYOS:FILE / A:FILE)", CommandExec);
    ConsoleRegisterAlias("execute", "exec");
    ConsoleRegister("kill", "signal user task (PR-P4)", CommandKill);
    ConsoleRegister("reboot", "reset CPU (QEMU display: quit+./run-split.sh)", CommandReboot);
    ConsoleRegister("halt", "stop CPU", CommandHalt);
    ConsoleRegisterAlias("halt", "exit");
    ConsoleRegisterAlias("halt", "quit");
}
