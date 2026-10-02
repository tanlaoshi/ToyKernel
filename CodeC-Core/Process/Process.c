/*
 * Process.c — exec / execve / 嵌入演示（PR-S-process-1）
 */
#include "Process.h"
#include "ProcessPrivate.h"
#include "Elf.h"
#include "CoreOps.h"
#include "Scheduler.h"
#include "VirtualMemory.h"
#include "Hal.h"
#include "Console.h"
#include "Debug.h"
#include "PhysicalMemory.h"
#include "Fat.h"

#define EXEC_ARGV_MAX 8
#define EXEC_ARG_LEN  64
#define EXEC_PATH_MAX 63

extern char _binary_User_hello_elf_start[];
extern char _binary_User_hello_elf_end[];

static int CopyUserCString(char *Dst, UINTN Max, UINT64 UserPtr) {
    UINTN i;
    char C;

    if (!Dst || Max == 0) {
        return -1;
    }
    if (UserPtr == 0) {
        Dst[0] = 0;
        return -1;
    }
    for (i = 0; i + 1 < Max; i++) {
        if (VirtualMemoryCopyFromUser(&C, UserPtr + i, 1) < 0) {
            Dst[0] = 0;
            return -1;
        }
        Dst[i] = C;
        if (C == 0) {
            return 0;
        }
    }
    Dst[Max - 1] = 0;
    return 0;
}

static void CopyPathName(char *Dst, int Max, const char *Path) {
    int i;
    const char *Base = Path;

    for (i = 0; Path[i]; i++) {
        if (Path[i] == '/' || Path[i] == '\\' || Path[i] == ':') {
            Base = &Path[i + 1];
        }
    }
    for (i = 0; i < Max - 1 && Base[i]; i++) {
        Dst[i] = Base[i];
    }
    Dst[i] = 0;
}

/*
 * 在新用户栈顶构造：argc | argv[] | NULL | envp NULL | 字符串区
 * 返回新 rsp（指向 argc；16 字节对齐，供 AAPCS64 / SysV CRT）
 */
static int ProcessSetupArgvStack(VIRTUAL_ADDRESS_SPACE *Space, UINT64 StackTop,
                                 char ArgBuf[][EXEC_ARG_LEN], int Argc,
                                 UINT64 *OutRsp) {
    UINT64 Sp = StackTop;
    UINT64 StrPtrs[EXEC_ARGV_MAX];
    UINT64 PtrSlot;
    UINT64 ArgcSlot;
    UINT64 Need;
    int i;
    UINTN Len;

    if (!Space || !OutRsp || Argc < 0 || Argc > EXEC_ARGV_MAX) {
        return -1;
    }

    /* 经 Space 页表写物理页，不 mov CR3——避免与定时器抢占互相踩页表 */
    for (i = Argc - 1; i >= 0; i--) {
        Len = 0;
        while (ArgBuf[i][Len] && Len + 1 < EXEC_ARG_LEN) {
            Len++;
        }
        Len++; /* NUL */
        Sp = (Sp - Len) & ~7ULL;
        if (VirtualMemoryCopyToSpace(Space, Sp, ArgBuf[i], Len) < 0) {
            return -1;
        }
        StrPtrs[i] = Sp;
    }

    /* argc + argv[]+NULL + envp NULL；整体 16 对齐 */
    Need = 8ULL * (UINT64)(Argc + 3);
    Sp = (Sp - Need) & ~0xFULL;
    ArgcSlot = Sp;
    PtrSlot = Sp + 8;
    {
        UINT64 Ac = (UINT64)(UINT32)Argc;
        UINT64 Z = 0;

        if (VirtualMemoryCopyToSpace(Space, ArgcSlot, &Ac, 8) < 0) {
            return -1;
        }
        for (i = 0; i < Argc; i++) {
            if (VirtualMemoryCopyToSpace(Space, PtrSlot + 8ULL * (UINT64)i, &StrPtrs[i],
                                        8) < 0) {
                return -1;
            }
        }
        if (VirtualMemoryCopyToSpace(Space, PtrSlot + 8ULL * (UINT64)Argc, &Z, 8) < 0 ||
            VirtualMemoryCopyToSpace(Space, PtrSlot + 8ULL * (UINT64)(Argc + 1), &Z, 8) <
                0) {
            return -1;
        }
    }

    *OutRsp = ArgcSlot;
    return 0;
}

int ProcessExec(const char *Path) {
    VIRTUAL_ADDRESS_SPACE *Space;
    ELF_LOAD_RESULT Info;
    UINT64 NewRsp;
    char Dummy[1][EXEC_ARG_LEN];

    if (!Path || !Path[0]) {
        ConsoleWrite("exec: empty path\n");
        return -1;
    }
    if (ProcessLoadPath(Path, &Space, &Info) != 0) {
        return -1;
    }
    /* CRT _start 读 (%rsp)=argc；须把 rsp 落到已映射栈页内（与 execve 一致） */
    if (ProcessSetupArgvStack(Space, Info.StackTop, Dummy, 0, &NewRsp) != 0) {
        VirtualMemorySpaceDestroy(Space);
        ConsoleWrite("exec: argv stack failed\n");
        return -1;
    }
    VirtualMemoryLoadPageTable(VirtualMemoryKernelRoot());
    Info.StackTop = NewRsp;
    HalInstallUserMode();
    /* 单用户 GUI：先停旧任务，避免 Raise 挪槽导致 wid 串台 */
    ProcessStopAllUsers();
    SchedulerReapOrphanZombies();
    ProcessApplyAppFont(Path);
    if (ProcessStartElf(Space, &Info, Path) != 0) {
        ProcessRestoreAppFont();
        return -1;
    }
    /* PR-A12 / B1：virt 平台形状无定时抢占 → 协作排空用户任务 */
    if (HalPlatformIsVirtSerialConsole()) {
        SchedulerCoopDrainUsers();
    }
    return 0;
}

/*
 * 替换当前用户任务映像。成功：改写 Frame，不返回用户态旧点；
 * 失败：返回 -1（Frame 原样，可写 rax=-1）。
 */
int ProcessExecve(HAL_INTERRUPT_FRAME *Frame, const char *Path, UINT64 UserArgv,
                  UINT64 UserEnvp) {
    TASK *T;
    VIRTUAL_ADDRESS_SPACE *OldSpace;
    VIRTUAL_ADDRESS_SPACE *NewSpace;
    ELF_LOAD_RESULT Info;
    char ArgBuf[EXEC_ARGV_MAX][EXEC_ARG_LEN];
    int Argc = 0;
    int i;
    UINT64 NewRsp;
    char Name[16];

    (void)UserEnvp;

    T = SchedulerCurrent();
    if (!T || !T->IsUser || !T->UserSpace || !Frame || !Path || !Path[0]) {
        return -1;
    }

    /* 先从旧地址空间拷出 argv（再销毁页表） */
    VirtualMemoryLoadPageTable(T->PageRoot);
    if (UserArgv != 0) {
        for (i = 0; i < EXEC_ARGV_MAX; i++) {
            UINT64 Ptr = 0;
            if (VirtualMemoryCopyFromUser(&Ptr, UserArgv + 8ULL * (UINT64)i, 8) < 0) {
                break;
            }
            if (Ptr == 0) {
                break;
            }
            if (CopyUserCString(ArgBuf[i], EXEC_ARG_LEN, Ptr) != 0) {
                break;
            }
            Argc++;
        }
    }
    VirtualMemoryLoadPageTable(VirtualMemoryKernelRoot());

    if (Argc == 0) {
        CopyPathName(ArgBuf[0], EXEC_ARG_LEN, Path);
        Argc = 1;
    }

    if (ProcessLoadPath(Path, &NewSpace, &Info) != 0) {
        return -1;
    }

    if (ProcessSetupArgvStack(NewSpace, Info.StackTop, ArgBuf, Argc, &NewRsp) != 0) {
        VirtualMemoryLoadPageTable(VirtualMemoryKernelRoot());
        VirtualMemorySpaceDestroy(NewSpace);
        ConsoleWrite("execve: argv stack failed\n");
        return -1;
    }

    OldSpace = T->UserSpace;
    T->UserSpace = NewSpace;
    T->PageRoot = VirtualMemorySpaceRoot(NewSpace);
    CopyPathName(Name, (int)sizeof(Name), Path);
    for (i = 0; i < 15 && Name[i]; i++) {
        T->Name[i] = Name[i];
    }
    T->Name[i] = 0;
    T->Waiting = 0;
    T->BrkBase = Info.BrkBase;
    T->Brk = Info.BrkBase;
    T->MmapNext = USER_MMAP_BASE;
    T->SigHandlerInt = 0;
    T->SigHandlerTerm = 0;
    T->PendingKill = 0;
    /* 保留 Fds / ParentId / Id；映像已换 */
    HalFrameSetUserEntry(Frame, Info.Entry, NewRsp);
    T->Frame = Frame;
    T->Started = 1; /* 经 sysret/iret 回到用户，非 HalUserEnter 首入 */

    VirtualMemoryLoadPageTable(T->PageRoot);
    VirtualMemorySpaceDestroy(OldSpace);

    HalInstallUserMode();
    ProcessApplyAppFont(Path);
    DebugWrite("execve: ");
    DebugWrite(Path);
    DebugWrite(" entry=");
    DebugHex64(Info.Entry);
    DebugWrite("\n");
    return 0;
}

int ProcessRunDemo(void) {
    UINTN Size = (UINTN)(_binary_User_hello_elf_end - _binary_User_hello_elf_start);
    if (Size == 0) {
        ConsoleWrite("user: no embedded hello.elf\n");
        return -1;
    }

    VIRTUAL_ADDRESS_SPACE *Space = VirtualMemorySpaceCreate();
    if (!Space) {
        ConsoleWrite("user: address space failed\n");
        return -1;
    }

    ELF_LOAD_RESULT Info;
    if (ElfLoadFromMemory(Space, _binary_User_hello_elf_start, Size, &Info) != 0) {
        VirtualMemorySpaceDestroy(Space);
        return -1;
    }

    HalInstallUserMode();
    return ProcessStartElf(Space, &Info, "hello");
}
