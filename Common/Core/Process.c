/*
 * Process.c — 用户进程加载、exec（Shell）与 execve（替换当前映像，PR-P1）
 */
#include "Process.h"
#include "Elf.h"
#include "FileSystem.h"
#include "Scheduler.h"
#include "VirtualMemory.h"
#include "Hal.h"
#include "Console.h"
#include "Debug.h"
#include "PhysicalMemory.h"

#define ELF_MAX_SIZE (512 * 1024)
#define EXEC_ARGV_MAX 8
#define EXEC_ARG_LEN  64
#define EXEC_PATH_MAX 63

extern char _binary_User_hello_elf_start[];
extern char _binary_User_hello_elf_end[];

static int ProcessStartElf(VM_ADDR_SPACE *Space, const ELF_LOAD_RESULT *Info,
                           const char *Name) {
    if (SchedulerCreateUser(Name, Info->Entry, Info->StackTop,
                        VirtualMemorySpaceRoot(Space), Space, Info->BrkBase) < 0) {
        ConsoleWrite("process: no task slot\n");
        return -1;
    }
    DebugWrite("process: started ");
    DebugWrite(Name);
    DebugWrite(" entry=");
    DebugHex64(Info->Entry);
    DebugWrite(" root=");
    DebugHex64(VirtualMemorySpaceRoot(Space));
    DebugWrite("\n");
    return 0;
}

static int ProcessLoadNeeded(VM_ADDR_SPACE *Space, const void *MainImage,
                             UINTN MainSize, ELF_SO_INFO *Sos, int *SoCount) {
    char Needed[ELF_MAX_NEEDED][16];
    int N;
    int i;
    UINT32 Pages;
    void *Buf;
    UINTN Size;

    *SoCount = 0;
    N = ElfCollectNeeded(MainImage, MainSize, Needed, ELF_MAX_NEEDED);
    if (N < 0) {
        ConsoleWrite("exec: parse DT_NEEDED failed\n");
        return -1;
    }
    if (N == 0) {
        return 0;
    }

    Pages = (ELF_MAX_SIZE + PAGE_SIZE - 1) / PAGE_SIZE;
    for (i = 0; i < N && *SoCount < ELF_MAX_SO; i++) {
        UINT64 Base = USER_SO_BASE + (UINT64)(*SoCount) * USER_SO_STRIDE;

        Buf = PhysicalMemoryAllocatePages(Pages);
        if (!Buf) {
            ConsoleWrite("exec: alloc so buffer failed\n");
            return -1;
        }
        Size = 0;
        if (FsReadFile(Needed[i], Buf, ELF_MAX_SIZE, &Size) != FAT_OK || Size < 64) {
            ConsoleWrite("exec: missing shared lib: ");
            ConsoleWrite(Needed[i]);
            ConsoleWrite("\n");
            PhysicalMemoryFreePages(Buf, Pages);
            return -1;
        }
        if (Size >= ELF_MAX_SIZE) {
            ConsoleWrite("exec: shared lib too large\n");
            PhysicalMemoryFreePages(Buf, Pages);
            return -1;
        }
        if (ElfLoadShared(Space, Buf, Size, Base, &Sos[*SoCount]) != 0) {
            ConsoleWrite("exec: load shared failed: ");
            ConsoleWrite(Needed[i]);
            ConsoleWrite("\n");
            PhysicalMemoryFreePages(Buf, Pages);
            return -1;
        }
        Sos[*SoCount].Image = (const UINT8 *)Buf;
        Sos[*SoCount].Size = Size;
        (*SoCount)++;
        DebugWrite("exec: loaded ");
        DebugWrite(Needed[i]);
        DebugWrite(" @");
        DebugHex64(Base);
        DebugWrite("\n");
    }
    return 0;
}

static void ProcessFreeSos(ELF_SO_INFO *Sos, int SoCount) {
    int i;
    UINT32 Pages = (ELF_MAX_SIZE + PAGE_SIZE - 1) / PAGE_SIZE;

    for (i = 0; i < SoCount; i++) {
        if (Sos[i].Image) {
            PhysicalMemoryFreePages((void *)(UINTN)Sos[i].Image, Pages);
            Sos[i].Image = 0;
        }
    }
}

/* 读盘并装载 ELF（含 DT_NEEDED）；成功时 *OutSpace 归属调用方 */
static int ProcessLoadPath(const char *Path, VM_ADDR_SPACE **OutSpace,
                           ELF_LOAD_RESULT *OutInfo) {
    UINT32 Pages;
    void *Buf;
    UINTN Size = 0;
    VM_ADDR_SPACE *Space;
    ELF_SO_INFO Sos[ELF_MAX_SO];
    int SoCount = 0;
    int i;

    if (!Path || !Path[0] || !OutSpace || !OutInfo) {
        return -1;
    }
    *OutSpace = 0;

    Pages = (ELF_MAX_SIZE + PAGE_SIZE - 1) / PAGE_SIZE;
    Buf = PhysicalMemoryAllocatePages(Pages);
    if (!Buf) {
        ConsoleWrite("exec: alloc buffer failed\n");
        return -1;
    }

    if (FsReadFile(Path, Buf, ELF_MAX_SIZE, &Size) != FAT_OK) {
        ConsoleWrite("exec: file not found: ");
        ConsoleWrite(Path);
        ConsoleWrite("\n");
        PhysicalMemoryFreePages(Buf, Pages);
        return -1;
    }
    if (Size < 64) {
        ConsoleWrite("exec: file too small\n");
        PhysicalMemoryFreePages(Buf, Pages);
        return -1;
    }
    if (Size >= ELF_MAX_SIZE) {
        ConsoleWrite("exec: file too large (max 512K)\n");
        PhysicalMemoryFreePages(Buf, Pages);
        return -1;
    }

    Space = VirtualMemorySpaceCreate();
    if (!Space) {
        ConsoleWrite("exec: address space failed\n");
        PhysicalMemoryFreePages(Buf, Pages);
        return -1;
    }

    if (ElfLoadFromMemory(Space, Buf, Size, OutInfo) != 0) {
        ConsoleWrite("exec: elf load failed\n");
        VirtualMemorySpaceDestroy(Space);
        PhysicalMemoryFreePages(Buf, Pages);
        return -1;
    }

    for (i = 0; i < ELF_MAX_SO; i++) {
        Sos[i].Image = 0;
        Sos[i].DynSym = 0;
        Sos[i].DynStr = 0;
        Sos[i].DynSymCount = 0;
        Sos[i].Base = 0;
        Sos[i].Size = 0;
    }

    VirtualMemoryLoadPageTable(VirtualMemorySpaceRoot(Space));

    if (ProcessLoadNeeded(Space, Buf, Size, Sos, &SoCount) != 0) {
        VirtualMemoryLoadPageTable(VirtualMemoryKernelRoot());
        ProcessFreeSos(Sos, SoCount);
        VirtualMemorySpaceDestroy(Space);
        PhysicalMemoryFreePages(Buf, Pages);
        return -1;
    }

    if (SoCount > 0) {
        if (ElfRelocateProgram(Space, Buf, Size, Sos, SoCount) != 0) {
            ConsoleWrite("exec: relocate failed\n");
            VirtualMemoryLoadPageTable(VirtualMemoryKernelRoot());
            ProcessFreeSos(Sos, SoCount);
            VirtualMemorySpaceDestroy(Space);
            PhysicalMemoryFreePages(Buf, Pages);
            return -1;
        }
    }

    VirtualMemoryLoadPageTable(VirtualMemoryKernelRoot());
    ProcessFreeSos(Sos, SoCount);
    PhysicalMemoryFreePages(Buf, Pages);
    *OutSpace = Space;
    return 0;
}

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
static int ProcessSetupArgvStack(VM_ADDR_SPACE *Space, UINT64 StackTop,
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

    VirtualMemoryLoadPageTable(VirtualMemorySpaceRoot(Space));

    for (i = Argc - 1; i >= 0; i--) {
        Len = 0;
        while (ArgBuf[i][Len] && Len + 1 < EXEC_ARG_LEN) {
            Len++;
        }
        Len++; /* NUL */
        Sp = (Sp - Len) & ~7ULL;
        if (VirtualMemoryCopyToUser(Sp, ArgBuf[i], Len) < 0) {
            VirtualMemoryLoadPageTable(VirtualMemoryKernelRoot());
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

        if (VirtualMemoryCopyToUser(ArgcSlot, &Ac, 8) < 0) {
            VirtualMemoryLoadPageTable(VirtualMemoryKernelRoot());
            return -1;
        }
        for (i = 0; i < Argc; i++) {
            if (VirtualMemoryCopyToUser(PtrSlot + 8ULL * (UINT64)i, &StrPtrs[i], 8) < 0) {
                VirtualMemoryLoadPageTable(VirtualMemoryKernelRoot());
                return -1;
            }
        }
        if (VirtualMemoryCopyToUser(PtrSlot + 8ULL * (UINT64)Argc, &Z, 8) < 0 ||
            VirtualMemoryCopyToUser(PtrSlot + 8ULL * (UINT64)(Argc + 1), &Z, 8) < 0) {
            VirtualMemoryLoadPageTable(VirtualMemoryKernelRoot());
            return -1;
        }
    }

    *OutRsp = ArgcSlot;
    return 0;
}

static UINT64 AlignUpPage(UINT64 V) {
    return (V + (UINT64)PAGE_SIZE - 1) & ~((UINT64)PAGE_SIZE - 1);
}

/*
 * PR-P3：扩展/收缩用户堆。NewBrk==0 查询当前 break。
 * 上限 USER_BRK_MAX（SO 基址），不盖栈。
 */
UINT64 ProcessBrk(UINT64 NewBrk) {
    TASK *T;
    VM_ADDR_SPACE *Space;
    UINT64 Old;
    UINT64 From;
    UINT64 To;
    UINT64 Va;

    T = SchedulerCurrent();
    if (!T || !T->IsUser || !T->UserSpace) {
        return (UINT64)(INT64)-1;
    }
    if (NewBrk == 0) {
        return T->Brk;
    }
    if (NewBrk < T->BrkBase || NewBrk > USER_BRK_MAX) {
        return (UINT64)(INT64)-1;
    }

    Space = T->UserSpace;
    Old = T->Brk;

    if (NewBrk > Old) {
        From = AlignUpPage(Old);
        To = AlignUpPage(NewBrk);
        for (Va = From; Va < To; Va += PAGE_SIZE) {
            UINT64 Pte = HalPageGetEntry(Space->Root, Va);
            void *Page;

            if ((Pte & HAL_PAGE_PRESENT) && (Pte & HAL_PAGE_USER)) {
                continue;
            }
            Page = PhysicalMemoryAllocatePage();
            if (!Page) {
                return (UINT64)(INT64)-1;
            }
            {
                UINT8 *B = (UINT8 *)Page;
                UINTN i;
                for (i = 0; i < PAGE_SIZE; i++) {
                    B[i] = 0;
                }
            }
            if (VirtualMemorySpaceMapPage(Space, Va, (UINT64)(UINTN)Page,
                                          PTE_PRESENT | PTE_WRITABLE | PTE_USER) != 0) {
                PhysicalMemoryFreePage(Page);
                return (UINT64)(INT64)-1;
            }
        }
    } else if (NewBrk < Old) {
        From = AlignUpPage(NewBrk);
        To = AlignUpPage(Old);
        for (Va = From; Va < To; Va += PAGE_SIZE) {
            UINT64 Pte = HalPageGetEntry(Space->Root, Va);
            UINT64 Phys;

            if (!(Pte & HAL_PAGE_PRESENT) || !(Pte & HAL_PAGE_USER)) {
                continue;
            }
            Phys = Pte & ~0xFFFULL;
            HalPageUnmapRange(Space->Root, Va, Va + PAGE_SIZE);
            PhysicalMemoryReleasePage((void *)(UINTN)Phys);
        }
    }

    T->Brk = NewBrk;
    return NewBrk;
}

int ProcessExec(const char *Path) {
    VM_ADDR_SPACE *Space;
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
    HalUserInstall();
    SchedulerReapOrphanZombies();
    if (ProcessStartElf(Space, &Info, Path) != 0) {
        return -1;
    }
    /* virt 无定时抢占：协作跑完刚创建的用户任务（PR-A12） */
    if (HalPlatformVirtConsole()) {
        SchedulerCoopDrainUsers();
    }
    return 0;
}

/*
 * 替换当前用户任务映像。成功：改写 Frame，不返回用户态旧点；
 * 失败：返回 -1（Frame 原样，可写 rax=-1）。
 */
int ProcessExecve(HAL_FRAME *Frame, const char *Path, UINT64 UserArgv,
                  UINT64 UserEnvp) {
    TASK *T;
    VM_ADDR_SPACE *OldSpace;
    VM_ADDR_SPACE *NewSpace;
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
    /* 保留 Fds / ParentId / Id；映像已换 */
    HalFrameSetUserEntry(Frame, Info.Entry, NewRsp);
    T->Frame = Frame;
    T->Started = 1; /* 经 sysret/iret 回到用户，非 HalUserEnter 首入 */

    VirtualMemoryLoadPageTable(T->PageRoot);
    VirtualMemorySpaceDestroy(OldSpace);

    HalUserInstall();
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

    VM_ADDR_SPACE *Space = VirtualMemorySpaceCreate();
    if (!Space) {
        ConsoleWrite("user: address space failed\n");
        return -1;
    }

    ELF_LOAD_RESULT Info;
    if (ElfLoadFromMemory(Space, _binary_User_hello_elf_start, Size, &Info) != 0) {
        VirtualMemorySpaceDestroy(Space);
        return -1;
    }

    HalUserInstall();
    return ProcessStartElf(Space, &Info, "hello");
}
