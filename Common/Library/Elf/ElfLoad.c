/*
 * ElfLoad.c — PR-S3-elf-1：静态 ET_EXEC 装载（段 + 用户栈）
 */
#include "ElfPrivate.h"
#include "ToySerialLog.h"

int ElfLoadFromMemory(VIRTUAL_ADDRESS_SPACE *Space, const void *Image, UINTN Size,
                      ELF_LOAD_RESULT *Out) {
    const UINT8 *Bytes = (const UINT8 *)Image;
    const Elf64_Ehdr *Hdr = (const Elf64_Ehdr *)Image;
    const Elf64_Phdr *Phdrs;
    UINT16 Pn;
    UINT16 i;
    UINT64 BrkBase = USER_CODE_VIRT;

    if (!ElfHeaderOk(Hdr, Size, ET_EXEC)) {
        ToyLogMem("elf: bad header\n");
        return -1;
    }
    if (ElfPhdrs(Bytes, Size, Hdr, &Phdrs, &Pn) != 0) {
        ToyLogMem("elf: phdr out of range\n");
        return -1;
    }

    for (i = 0; i < Pn; i++) {
        UINT64 SegEnd;

        if (Phdrs[i].p_type != PT_LOAD) {
            continue;
        }
        if (ElfMapSegment(Space, Bytes, &Phdrs[i], 0) != 0) {
            ToyLogMem("elf: map segment failed\n");
            return -1;
        }
        SegEnd = Phdrs[i].p_vaddr + Phdrs[i].p_memsz;
        if (SegEnd > BrkBase) {
            BrkBase = SegEnd;
        }
    }

    if (ElfMapStack(Space) != 0) {
        ToyLogMem("elf: map stack failed\n");
        return -1;
    }

    if (Out) {
        Out->Entry = Hdr->e_entry;
        Out->StackTop = USER_STACK_VIRT + USER_STACK_SIZE;
        Out->BrkBase = BrkBase;
    }
    return 0;
}
