/*
 * Process.c — 用户进程加载与 exec
 */
#include "Process.h"
#include "Elf.h"
#include "Fat.h"
#include "Sched.h"
#include "Vmm.h"
#include "hal.h"
#include "Console.h"
#include "Pmm.h"

#define ELF_MAX_SIZE (64 * 1024)

extern char _binary_user_hello_elf_start[];
extern char _binary_user_hello_elf_end[];

static int ProcessStartElf(VM_ADDR_SPACE *Space, const ELF_LOAD_RESULT *Info,
                           const char *Name) {
    if (SchedCreateUser(Name, Info->Entry, Info->StackTop,
                        VmmSpaceCr3(Space), Space) < 0) {
        ConsoleWrite("process: no task slot\n");
        return -1;
    }
    ConsoleWrite("process: started ");
    ConsoleWrite(Name);
    ConsoleWrite(" entry=");
    ConsoleHex64(Info->Entry);
    ConsoleWrite(" cr3=");
    ConsoleHex64(VmmSpaceCr3(Space));
    ConsoleWrite("\n");
    return 0;
}

int ProcessExec(const char *Path) {
    void *Buf = PmmAllocPages((ELF_MAX_SIZE + PAGE_SIZE - 1) / PAGE_SIZE);
    if (!Buf) {
        ConsoleWrite("exec: alloc buffer failed\n");
        return -1;
    }

    UINTN Size = 0;
    if (!FatReadFile(Path, Buf, ELF_MAX_SIZE, &Size)) {
        ConsoleWrite("exec: file not found: ");
        ConsoleWrite(Path);
        ConsoleWrite("\n");
        PmmFreePages(Buf, (ELF_MAX_SIZE + PAGE_SIZE - 1) / PAGE_SIZE);
        return -1;
    }

    VM_ADDR_SPACE *Space = VmmSpaceCreate();
    if (!Space) {
        ConsoleWrite("exec: address space failed\n");
        PmmFreePages(Buf, (ELF_MAX_SIZE + PAGE_SIZE - 1) / PAGE_SIZE);
        return -1;
    }

    ELF_LOAD_RESULT Info;
    if (ElfLoadFromMemory(Space, Buf, Size, &Info) != 0) {
        VmmSpaceDestroy(Space);
        PmmFreePages(Buf, (ELF_MAX_SIZE + PAGE_SIZE - 1) / PAGE_SIZE);
        return -1;
    }

    PmmFreePages(Buf, (ELF_MAX_SIZE + PAGE_SIZE - 1) / PAGE_SIZE);

    HalUserInstall();
    return ProcessStartElf(Space, &Info, Path);
}

int UserRunDemo(void) {
    UINTN Size = (UINTN)(_binary_user_hello_elf_end - _binary_user_hello_elf_start);
    if (Size == 0) {
        ConsoleWrite("user: no embedded hello.elf\n");
        return -1;
    }

    VM_ADDR_SPACE *Space = VmmSpaceCreate();
    if (!Space) {
        ConsoleWrite("user: address space failed\n");
        return -1;
    }

    ELF_LOAD_RESULT Info;
    if (ElfLoadFromMemory(Space, _binary_user_hello_elf_start, Size, &Info) != 0) {
        VmmSpaceDestroy(Space);
        return -1;
    }

    HalUserInstall();
    return ProcessStartElf(Space, &Info, "hello");
}
