/*
 * ProcessLoad.c — 读盘装载 ELF（PR-S-process-1）
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

#define ELF_MAX_SIZE (512 * 1024)

int ProcessStartElf(VIRTUAL_ADDRESS_SPACE *Space, const ELF_LOAD_RESULT *Info,
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

static int ProcessLoadNeeded(VIRTUAL_ADDRESS_SPACE *Space, const void *MainImage,
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
        if (VfsServiceReadFile(Needed[i], Buf, ELF_MAX_SIZE, &Size) != FAT_OK || Size < 64) {
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
int ProcessLoadPath(const char *Path, VIRTUAL_ADDRESS_SPACE **OutSpace,
                           ELF_LOAD_RESULT *OutInfo) {
    UINT32 Pages;
    void *Buf;
    UINTN Size = 0;
    VIRTUAL_ADDRESS_SPACE *Space;
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

    if (VfsServiceReadFile(Path, Buf, ELF_MAX_SIZE, &Size) != FAT_OK) {
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

    /* 不切换 CR3：LoadNeeded 只改 Space 页表；重定位经 CopyToSpace 写物理页 */
    if (ProcessLoadNeeded(Space, Buf, Size, Sos, &SoCount) != 0) {
        ProcessFreeSos(Sos, SoCount);
        VirtualMemorySpaceDestroy(Space);
        PhysicalMemoryFreePages(Buf, Pages);
        return -1;
    }

    if (SoCount > 0) {
        if (ElfRelocateProgram(Space, Buf, Size, Sos, SoCount) != 0) {
            ConsoleWrite("exec: relocate failed\n");
            ProcessFreeSos(Sos, SoCount);
            VirtualMemorySpaceDestroy(Space);
            PhysicalMemoryFreePages(Buf, Pages);
            return -1;
        }
    }

    ProcessFreeSos(Sos, SoCount);
    PhysicalMemoryFreePages(Buf, Pages);
    *OutSpace = Space;
    return 0;
}
