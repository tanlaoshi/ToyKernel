/*
 * Elf.c — 静态 ET_EXEC ELF64 加载器（无重定位）
 */
#include "Elf.h"
#include "Pmm.h"
#include "Console.h"

static void MemZero(void *Ptr, UINTN Size) {
    UINT8 *B = (UINT8 *)Ptr;
    for (UINTN i = 0; i < Size; i++) {
        B[i] = 0;
    }
}

static int ElfValid(const Elf64_Ehdr *Hdr, UINTN Size) {
    if (Size < sizeof(Elf64_Ehdr)) {
        return 0;
    }
    if (*(UINT32 *)&Hdr->e_ident[0] != ELF_MAGIC) {
        return 0;
    }
    if (Hdr->e_ident[4] != ELFCLASS64 || Hdr->e_ident[5] != ELFDATA2LSB) {
        return 0;
    }
    if (Hdr->e_type != ET_EXEC || Hdr->e_machine != EM_X86_64) {
        return 0;
    }
    if (Hdr->e_phentsize != sizeof(Elf64_Phdr)) {
        return 0;
    }
    return 1;
}

static UINT64 ElfAlignUp(UINT64 Value, UINT64 Align) {
    if (Align <= 1) {
        return Value;
    }
    return (Value + Align - 1) & ~(Align - 1);
}

static int ElfMapSegment(VM_ADDR_SPACE *Space, const UINT8 *Image,
                         const Elf64_Phdr *Ph) {
    UINT64 MapStart = Ph->p_vaddr & ~(UINT64)(PAGE_SIZE - 1);
    UINT64 MapEnd = ElfAlignUp(Ph->p_vaddr + Ph->p_memsz, PAGE_SIZE);
    UINT64 Flags = PTE_PRESENT | PTE_USER;
    if (Ph->p_flags & PF_W) {
        Flags |= PTE_WRITABLE;
    }

    for (UINT64 Virt = MapStart; Virt < MapEnd; Virt += PAGE_SIZE) {
        void *Page = VmmSpaceAllocAndTrack(Space);
        if (!Page) {
            return -1;
        }
        MemZero(Page, PAGE_SIZE);

        for (UINT64 Off = 0; Off < PAGE_SIZE; Off++) {
            UINT64 Va = Virt + Off;
            if (Va < Ph->p_vaddr || Va >= Ph->p_vaddr + Ph->p_memsz) {
                continue;
            }
            if (Va < Ph->p_vaddr + Ph->p_filesz) {
                UINT64 Src = Ph->p_offset + (Va - Ph->p_vaddr);
                ((UINT8 *)Page)[Off] = Image[Src];
            }
        }

        if (VmmSpaceMapPage(Space, Virt, (UINT64)(UINTN)Page, Flags) != 0) {
            return -1;
        }
    }
    return 0;
}

static int ElfMapStack(VM_ADDR_SPACE *Space) {
    UINT64 Flags = PTE_PRESENT | PTE_WRITABLE | PTE_USER;
    for (UINT64 Virt = USER_STACK_VIRT;
         Virt < USER_STACK_VIRT + USER_STACK_SIZE;
         Virt += PAGE_SIZE) {
        void *Page = VmmSpaceAllocAndTrack(Space);
        if (!Page) {
            return -1;
        }
        MemZero(Page, PAGE_SIZE);
        if (VmmSpaceMapPage(Space, Virt, (UINT64)(UINTN)Page, Flags) != 0) {
            return -1;
        }
    }
    return 0;
}

int ElfLoadFromMemory(VM_ADDR_SPACE *Space, const void *Image, UINTN Size,
                      ELF_LOAD_RESULT *Out) {
    const UINT8 *Bytes = (const UINT8 *)Image;
    const Elf64_Ehdr *Hdr = (const Elf64_Ehdr *)Image;

    if (!ElfValid(Hdr, Size)) {
        ConsoleWrite("elf: bad header\n");
        return -1;
    }

    if (Hdr->e_phoff + (UINT64)Hdr->e_phnum * sizeof(Elf64_Phdr) > Size) {
        ConsoleWrite("elf: phdr out of range\n");
        return -1;
    }

    const Elf64_Phdr *Phdrs = (const Elf64_Phdr *)(Bytes + Hdr->e_phoff);
    for (UINT16 i = 0; i < Hdr->e_phnum; i++) {
        if (Phdrs[i].p_type != PT_LOAD) {
            continue;
        }
        if (ElfMapSegment(Space, Bytes, &Phdrs[i]) != 0) {
            ConsoleWrite("elf: map segment failed\n");
            return -1;
        }
    }

    if (ElfMapStack(Space) != 0) {
        ConsoleWrite("elf: map stack failed\n");
        return -1;
    }

    if (Out) {
        Out->Entry = Hdr->e_entry;
        Out->StackTop = USER_STACK_VIRT + USER_STACK_SIZE;
    }
    return 0;
}
