/*
 * Process.c — 用户进程加载与 exec
 */
#include "Process.h"
#include "Elf.h"
#include "Fat.h"
#include "Scheduler.h"
#include "VirtualMemory.h"
#include "hal.h"
#include "Console.h"
#include "Debug.h"
#include "PhysicalMemory.h"

#define ELF_MAX_SIZE (512 * 1024)

extern char _binary_User_hello_elf_start[];
extern char _binary_User_hello_elf_end[];

static int ProcessStartElf(VM_ADDR_SPACE *Space, const ELF_LOAD_RESULT *Info,
                           const char *Name) {
    if (SchedulerCreateUser(Name, Info->Entry, Info->StackTop,
                        VirtualMemorySpaceCr3(Space), Space) < 0) {
        ConsoleWrite("process: no task slot\n");
        return -1;
    }
    DebugWrite("process: started ");
    DebugWrite(Name);
    DebugWrite(" entry=");
    DebugHex64(Info->Entry);
    DebugWrite(" cr3=");
    DebugHex64(VirtualMemorySpaceCr3(Space));
    DebugWrite("\n");
    return 0;
}

int ProcessExec(const char *Path) {
    UINT32 Pages;
    void *Buf;
    UINTN Size = 0;
    VM_ADDR_SPACE *Space;
    ELF_LOAD_RESULT Info;

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

    if (!FatReadFile(Path, Buf, ELF_MAX_SIZE, &Size)) {
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
