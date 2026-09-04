/*
 * Process.c — 用户进程加载与 exec（支持 DT_NEEDED 简易动态链接）
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

extern char _binary_User_hello_elf_start[];
extern char _binary_User_hello_elf_end[];

static int ProcessStartElf(VM_ADDR_SPACE *Space, const ELF_LOAD_RESULT *Info,
                           const char *Name) {
    if (SchedulerCreateUser(Name, Info->Entry, Info->StackTop,
                        VirtualMemorySpaceRoot(Space), Space) < 0) {
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
        /*
         * SO 符号表指针指向 Buf；重定位完成前不能释放。
         * 暂把 Image 留在 Sos 中，由调用方在 Relocate 后释放。
         */
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

int ProcessExec(const char *Path) {
    UINT32 Pages;
    void *Buf;
    UINTN Size = 0;
    VM_ADDR_SPACE *Space;
    ELF_LOAD_RESULT Info;
    ELF_SO_INFO Sos[ELF_MAX_SO];
    int SoCount = 0;
    int i;

    if (!Path || !Path[0]) {
        ConsoleWrite("exec: empty path\n");
        return -1;
    }

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

    if (ElfLoadFromMemory(Space, Buf, Size, &Info) != 0) {
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

    /* 装载 DT_NEEDED 前先切到用户页表，便于 CopyToUser 写 GOT */
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

    HalUserInstall();
    SchedulerReapOrphanZombies();
    return ProcessStartElf(Space, &Info, Path);
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
