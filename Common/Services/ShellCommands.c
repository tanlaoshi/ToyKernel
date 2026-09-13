/*
 * ShellCommands.c — Shell 扩展命令（mem / exec / net / ping 等）
 */
#include "ShellCommands.h"
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
#include "Gui.h"
#include "Locale.h"
#include "Font.h"
#include "Theme.h"
#include "Store.h"
#include "Fat.h"

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

static void CommandShell(int Argc, char **Argv) {
    int Idx;

    (void)Argc;
    (void)Argv;
    Idx = GuiOpenShell();
    if (Idx < 0) {
        ConsoleWrite("shell: no free window\n");
        return;
    }
    ConsoleOnShellOpened();
}

static void CommandSettings(int Argc, char **Argv) {
    int Idx;

    (void)Argc;
    (void)Argv;
    Idx = GuiOpenSettings();
    if (Idx < 0) {
        ConsoleWrite("settings: no free window\n");
    }
}

static void CommandFiles(int Argc, char **Argv) {
    int Idx;

    (void)Argc;
    (void)Argv;
    Idx = GuiOpenFiles();
    if (Idx < 0) {
        ConsoleWrite("files: no free window\n");
    }
}

static void CommandEdit(int Argc, char **Argv) {
    int Idx;
    const char *Path;

    if (Argc < 2 || !Argv[1] || !Argv[1][0]) {
        ConsoleWrite("usage: edit <path>\n");
        return;
    }
    Path = Argv[1];
    Idx = GuiOpenEdit(Path);
    if (Idx < 0) {
        ConsoleWrite("edit: no free window\n");
    }
}

static void CommandZh(int Argc, char **Argv) {
    (void)Argc;
    (void)Argv;
    /* PR-I18N1：UTF-8 + CJK16 子集冒烟 */
    ConsoleWrite("你好，世界！中文测试\n");
}

static int StoreWordEq(const char *A, const char *B) {
    if (A == 0 || B == 0) {
        return 0;
    }
    while (*A && *B) {
        if (*A != *B) {
            return 0;
        }
        A++;
        B++;
    }
    return *A == *B;
}

static void StorePrintUsage(void) {
    ConsoleWrite(
        "usage: store <list|install|remove|combo|uncombo|installed|sync|fetch|repo> ...\n");
    ConsoleWrite(
        "  aliases: (none)→list, status→list, rm→remove, list-installed→installed\n");
}

static void StoreCmdListCatalog(void) {
    STORE_ENTRY *Tab;
    int Count = 0;
    int i;
    int Err;

    Tab = StoreScratchTab();
    Err = StoreLoadCatalog(Tab, STORE_ENTRIES_MAX, &Count);
    if (Err < 0) {
        ConsoleWrite("store: catalog ");
        ConsoleWrite(FatStrError(Err));
        ConsoleWrite("\n");
        return;
    }
    ConsoleWrite("store (");
    ConsoleWrite(StoreHostArch());
    ConsoleWrite(")  state=INST|avail  dep=...\n");
    for (i = 0; i < Count; i++) {
        char Dep[64];
        int On = StoreIsInstalled(Tab[i].Id);
        if (On) {
            (void)StoreGetDepends(Tab[i].Id, Dep, (int)sizeof(Dep));
        } else if (Tab[i].Depends[0]) {
            int k = 0;
            while (Tab[i].Depends[k] && k < (int)sizeof(Dep) - 1) {
                Dep[k] = Tab[i].Depends[k];
                k++;
            }
            Dep[k] = 0;
        } else {
            Dep[0] = '-';
            Dep[1] = 0;
        }
        ConsoleWrite("  ");
        ConsoleWrite(Tab[i].Id);
        ConsoleWrite("  ");
        ConsoleWrite(Tab[i].Type);
        ConsoleWrite(On ? "  INST  " : "  avail ");
        ConsoleWrite("dep=");
        ConsoleWrite(Dep);
        ConsoleWrite("  ");
        ConsoleWrite(Tab[i].File);
        ConsoleWrite("  ");
        ConsoleWrite(Tab[i].Title);
        ConsoleWrite("\n");
    }
}

static void CommandStore(int Argc, char **Argv) {
    const char *Sub;
    int i;
    int Err;
    UINT32 Ip;
    UINT16 Port;
    char IpBuf[24];

    /* PR-C3：正统二级无中横线；别名在此展开 */
    if (Argc < 2) {
        Sub = "list";
    } else {
        Sub = Argv[1];
        if (StoreWordEq(Sub, "status")) {
            Sub = "list";
        } else if (StoreWordEq(Sub, "rm")) {
            Sub = "remove";
        } else if (StoreWordEq(Sub, "list-installed")) {
            Sub = "installed";
        }
    }

    if (StoreWordEq(Sub, "repo")) {
        if (Argc >= 3) {
            if (StoreRepoSet(Argv[2]) != 0) {
                ConsoleWrite("store repo: bad ip:port\n");
                return;
            }
            ConsoleWrite("store: repo set\n");
            return;
        }
        StoreRepoGet(&Ip, &Port);
        HalNetFormatIp(Ip, IpBuf, (int)sizeof(IpBuf));
        ConsoleWrite("store repo ");
        ConsoleWrite(IpBuf);
        ConsoleWrite(":");
        ConsoleWriteHex32(Port);
        ConsoleWrite("\n");
        return;
    }

    if (StoreWordEq(Sub, "sync")) {
        Err = StoreSyncCatalog();
        if (Err == -41 || Err == -2) {
            ConsoleWrite("store sync: HTTP not 200 (host http.server + /catalog.txt?)\n");
            return;
        }
        if (Err == -40) {
            ConsoleWrite("store sync: net/tcp fail (repo up? store repo)\n");
            return;
        }
        if (Err == -43) {
            ConsoleWrite("store sync: out of memory\n");
            return;
        }
        if (Err != 0) {
            ConsoleWrite("store sync: ");
            ConsoleWrite(FatStrError(Err));
            ConsoleWrite("\n");
            return;
        }
        ConsoleWrite("store: synced Store/catalog.txt\n");
        return;
    }

    if (StoreWordEq(Sub, "fetch")) {
        if (Argc < 3) {
            ConsoleWrite("usage: store fetch <id>\n");
            return;
        }
        Err = StoreFetchId(Argv[2]);
        if (Err == -41 || Err == -2) {
            ConsoleWrite("store fetch: HTTP not 200\n");
            return;
        }
        if (Err == -42 || Err == -3) {
            ConsoleWrite("store fetch: hash mismatch\n");
            return;
        }
        if (Err == -40) {
            ConsoleWrite("store fetch: net/tcp fail\n");
            return;
        }
        if (Err == -43) {
            ConsoleWrite("store fetch: out of memory\n");
            return;
        }
        if (Err != 0) {
            ConsoleWrite("store fetch: ");
            ConsoleWrite(FatStrError(Err));
            ConsoleWrite("\n");
            return;
        }
        ConsoleWrite("store: fetched to Store/\n");
        ConsoleWrite("hint: store install <id>\n");
        return;
    }

    if (StoreWordEq(Sub, "install")) {
        if (Argc < 3) {
            ConsoleWrite("usage: store install <id>\n");
            return;
        }
        Err = StoreInstall(Argv[2]);
        if (Err != FAT_OK) {
            ConsoleWrite("store install: ");
            ConsoleWrite(FatStrError(Err));
            ConsoleWrite("\n");
            ConsoleWrite("hint: store combo <id> installs depends first\n");
            return;
        }
        {
            STORE_ENTRY *Tab2 = StoreScratchTab();
            int C2 = 0;
            int j;
            const char *Where = "Apps/";
            (void)StoreLoadCatalog(Tab2, STORE_ENTRIES_MAX, &C2);
            for (j = 0; j < C2; j++) {
                int k = 0;
                while (Argv[2][k] && Argv[2][k] == Tab2[j].Id[k]) {
                    k++;
                }
                if (Argv[2][k] == 0 && Tab2[j].Id[k] == 0) {
                    if (Tab2[j].Type[0] == 'f') {
                        Where = "Assets/Fonts/";
                    } else if (Tab2[j].Type[0] == 'a' && Tab2[j].Type[1] == 's') {
                        Where = "Assets/Packs/";
                    }
                    break;
                }
            }
            ConsoleWrite("store: installed to ");
            ConsoleWrite(Where);
            ConsoleWrite("\n");
            if (Where[0] == 'A' && Where[7] == 'F') {
                ConsoleWrite("hint: font / font reload — Settings 可选新字面\n");
            } else if (Where[0] == 'A' && Where[7] == 'P') {
                ConsoleWrite("hint: blob under Assets/Packs/ (driver reads path)\n");
            } else {
                ConsoleWrite("hint: exec Apps/<ELF> — HELLO prints one line then exits (正常)\n");
            }
        }
        return;
    }

    if (StoreWordEq(Sub, "installed")) {
        STORE_INSTALLED Inst[STORE_INSTALLED_MAX];
        int N = 0;
        Err = StoreListInstalled(Inst, STORE_INSTALLED_MAX, &N);
        if (Err != FAT_OK) {
            ConsoleWrite("store installed: ");
            ConsoleWrite(FatStrError(Err));
            ConsoleWrite("\n");
            return;
        }
        ConsoleWrite("store installed:\n");
        if (N == 0) {
            ConsoleWrite("  (none)\n");
            return;
        }
        for (i = 0; i < N; i++) {
            char Dep[64];
            ConsoleWrite("  ");
            ConsoleWrite(Inst[i].Id);
            ConsoleWrite("  ");
            ConsoleWrite(Inst[i].Type);
            ConsoleWrite("  ");
            ConsoleWrite(Inst[i].File);
            ConsoleWrite("  dep=");
            (void)StoreGetDepends(Inst[i].Id, Dep, (int)sizeof(Dep));
            ConsoleWrite(Dep);
            ConsoleWrite("\n");
        }
        return;
    }

    if (StoreWordEq(Sub, "remove")) {
        if (Argc < 3) {
            ConsoleWrite("usage: store remove <id>\n");
            return;
        }
        Err = StoreRemove(Argv[2]);
        if (Err != FAT_OK) {
            ConsoleWrite("store remove: ");
            ConsoleWrite(FatStrError(Err));
            ConsoleWrite("\n");
            ConsoleWrite("hint: still required? store uncombo <leaf>\n");
            return;
        }
        ConsoleWrite("store: removed ");
        ConsoleWrite(Argv[2]);
        ConsoleWrite("\n");
        return;
    }

    if (StoreWordEq(Sub, "combo")) {
        if (Argc < 3) {
            ConsoleWrite("usage: store combo <id>\n");
            ConsoleWrite("hint: e.g. store combo guidemo  (demopack+sun8 then app)\n");
            return;
        }
        Err = StoreComboInstall(Argv[2]);
        if (Err != FAT_OK) {
            ConsoleWrite("store combo: ");
            ConsoleWrite(FatStrError(Err));
            ConsoleWrite("\n");
            return;
        }
        ConsoleWrite("store: combo installed ");
        ConsoleWrite(Argv[2]);
        ConsoleWrite(" (+depends)\n");
        return;
    }

    if (StoreWordEq(Sub, "uncombo")) {
        if (Argc < 3) {
            ConsoleWrite("usage: store uncombo <id>\n");
            return;
        }
        Err = StoreComboRemove(Argv[2]);
        if (Err != FAT_OK) {
            ConsoleWrite("store uncombo: ");
            ConsoleWrite(FatStrError(Err));
            ConsoleWrite("\n");
            return;
        }
        ConsoleWrite("store: combo removed ");
        ConsoleWrite(Argv[2]);
        ConsoleWrite("\n");
        return;
    }

    if (StoreWordEq(Sub, "list")) {
        StoreCmdListCatalog();
        return;
    }

    StorePrintUsage();
}

static void CommandFont(int Argc, char **Argv) {
    UINT32 i;
    UINT32 Id;
    const FONT_FACE *F;

    if (Argc >= 2 && Argv[1][0] == 'r' && Argv[1][1] == 'e' &&
        Argv[1][2] == 'l' && Argv[1][3] == 'o' && Argv[1][4] == 'a' &&
        Argv[1][5] == 'd' && Argv[1][6] == 0) {
        (void)FontReloadAssets();
        ThemeClampFontId();
        ConsoleWrite("font: assets reloaded\n");
        return;
    }
    if (Argc >= 2 && Argv[1][0] >= '0' && Argv[1][0] <= '9') {
        Id = 0;
        for (i = 0; Argv[1][i] >= '0' && Argv[1][i] <= '9'; i++) {
            Id = Id * 10u + (UINT32)(Argv[1][i] - '0');
        }
        if (Argv[1][i] != 0 || ThemeSetFontId(Id) != 0) {
            ConsoleWrite("font: bad id\n");
            return;
        }
        ThemeApply();
        ConsoleWrite("font: set ");
        F = FontGetCurrent();
        ConsoleWrite(F && F->Name ? F->Name : "?");
        ConsoleWrite("\n");
        return;
    }
    ConsoleWrite("fonts:\n");
    for (i = 0; i < FontCount(); i++) {
        F = FontGetById(i);
        ConsoleWrite(i == FontCurrentId() ? " * " : "   ");
        ConsoleWrite(F && F->Name ? F->Name : "?");
        ConsoleWrite("\n");
    }
    if (Argc < 2) {
        ConsoleWrite("usage: font [reload|<id>]\n");
    }
}

static void CommandLang(int Argc, char **Argv) {
    if (Argc < 2) {
        ConsoleWrite(LocStr(MSG_LANG_USAGE));
        ConsoleWrite(LocStr(MSG_LANG_NOW));
        ConsoleWrite(LocaleGet() == LOC_LANG_ZH ? "zh\n" : "en\n");
        return;
    }
    if (Argv[1][0] == 'r' && Argv[1][1] == 'e' && Argv[1][2] == 'l' &&
        Argv[1][3] == 'o' && Argv[1][4] == 'a' && Argv[1][5] == 'd' &&
        Argv[1][6] == 0) {
        /* lang reload — 重读 Assets/Locale/en.txt 与 zh.txt */
        LocaleReload();
        ConsoleWrite("locale reloaded\n");
        return;
    }
    if (Argv[1][0] == 'z' && Argv[1][1] == 'h') {
        (void)LocaleSet(LOC_LANG_ZH);
        ConsoleWrite(LocStr(MSG_LANG_SET));
        return;
    }
    if (Argv[1][0] == 'e' && Argv[1][1] == 'n') {
        (void)LocaleSet(LOC_LANG_EN);
        ConsoleWrite(LocStr(MSG_LANG_SET));
        return;
    }
    ConsoleWrite(LocStr(MSG_LANG_BAD));
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

void ShellCommandsRegisterVirtMin(void) {
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

void ShellCommandsRegister(void) {
    /* list / show / test / run / set（目录类二级在 ShellCommandsRegisterFs） */
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
    ShellCmdUsbRegister();
    ShellCmdNetRegister();

    ConsoleRegister2("test", "memory", "alloc/verify/free one page", CommandMemtest);
    ConsoleRegister2("test", "glyph", "UTF-8 Chinese glyph test", CommandZh);
    ConsoleRegisterAliasLine("memtest", "test", "memory");
    ConsoleRegisterAliasLine("zh", "test", "glyph");

    ConsoleRegister2("run", "user", "run embedded hello ELF", CommandRunuser);
    ConsoleRegisterAliasLine("runuser", "run", "user");

    ConsoleRegister2("set", "language", "set language en|zh|reload", CommandLang);
    ConsoleRegister2("set", "priority", "set priority <pid> <prio>", CommandSetPriority);
    ConsoleRegisterAliasLine("lang", "set", "language");
    ConsoleRegisterAliasLine("language", "set", "language");
    ConsoleRegisterAliasLine("nice", "set", "priority");

    ConsoleRegister("execute", "load ELF (TOYOS:FILE / A:FILE)", CommandExec);
    ConsoleRegisterAlias("execute", "exec");
    ConsoleRegister("kill", "signal user task (PR-P4)", CommandKill);
    ConsoleRegister("shell", "open Shell window", CommandShell);
    ConsoleRegister("settings", "open Settings window", CommandSettings);
    ConsoleRegister("files", "open Files browser", CommandFiles);
    ConsoleRegister("edit", "edit <path> open text editor (PR-V2)", CommandEdit);
    ConsoleRegister("font", "font [reload|<id>] (Assets/Fonts TOYF)", CommandFont);
    ConsoleRegister("store", "store list|install|remove|combo|uncombo|installed|…", CommandStore);
    ConsoleRegister("reboot", "reset CPU (QEMU display: quit+./run-split.sh)", CommandReboot);
    ConsoleRegister("halt", "stop CPU", CommandHalt);
    ConsoleRegisterAlias("halt", "exit");
    ConsoleRegisterAlias("halt", "quit");
}
