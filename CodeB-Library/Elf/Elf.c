/*
 * Elf.c — PR-S3-elf-1：ELF64 公共帮手（头校验 / PT_LOAD 映射 / 动态段）
 *
 * Load / Reloc / So 见同目录其它文件。
 */
#include "ElfPrivate.h"
#include "PhysicalMemory.h"
#include "Hal.h"

void ElfZeroMemory(void *Ptr, UINTN Size) {
    UINT8 *B = (UINT8 *)Ptr;
    for (UINTN i = 0; i < Size; i++) {
        B[i] = 0;
    }
}

int ElfStrEq(const char *A, const char *B) {
    if (!A || !B) {
        return 0;
    }
    while (*A && *A == *B) {
        A++;
        B++;
    }
    return *A == *B;
}

int ElfHeaderOk(const Elf64_Ehdr *Hdr, UINTN Size, UINT16 WantType) {
    if (Size < sizeof(Elf64_Ehdr)) {
        return 0;
    }
    if (*(UINT32 *)&Hdr->e_ident[0] != ELF_MAGIC) {
        return 0;
    }
    if (Hdr->e_ident[4] != ELFCLASS64 || Hdr->e_ident[5] != ELFDATA2LSB) {
        return 0;
    }
    if (Hdr->e_type != WantType || Hdr->e_machine != HalElfMachine()) {
        return 0;
    }
    if (Hdr->e_phentsize != sizeof(Elf64_Phdr)) {
        return 0;
    }
    return 1;
}

UINT64 ElfAlignUp(UINT64 Value, UINT64 Align) {
    if (Align <= 1) {
        return Value;
    }
    return (Value + Align - 1) & ~(Align - 1);
}

int ElfMapSegment(VIRTUAL_ADDRESS_SPACE *Space, const UINT8 *Image,
                  const Elf64_Phdr *Ph, UINT64 Bias) {
    UINT64 Vaddr = Ph->p_vaddr + Bias;
    UINT64 MapStart = Vaddr & ~(UINT64)(PAGE_SIZE - 1);
    UINT64 MapEnd = ElfAlignUp(Vaddr + Ph->p_memsz, PAGE_SIZE);
    UINT64 Flags = PTE_PRESENT | PTE_USER;

    if (Ph->p_memsz == 0) {
        return 0;
    }
    if (Ph->p_flags & PF_W) {
        Flags |= PTE_WRITABLE;
    }

    for (UINT64 Virt = MapStart; Virt < MapEnd; Virt += PAGE_SIZE) {
        void *Page = PhysicalMemoryAllocatePage();
        if (!Page) {
            return -1;
        }
        ElfZeroMemory(Page, PAGE_SIZE);

        for (UINT64 Off = 0; Off < PAGE_SIZE; Off++) {
            UINT64 Va = Virt + Off;
            if (Va < Vaddr || Va >= Vaddr + Ph->p_memsz) {
                continue;
            }
            if (Va < Vaddr + Ph->p_filesz) {
                UINT64 Src = Ph->p_offset + (Va - Vaddr);
                ((UINT8 *)Page)[Off] = Image[Src];
            }
        }

        if (VirtualMemorySpaceMapPage(Space, Virt, (UINT64)(UINTN)Page, Flags) != 0) {
            PhysicalMemoryFreePage(Page);
            return -1;
        }
        /* Arm/RiscV：可执行映像经 D-cache 写入后须刷 I-cache（PR-A12） */
        HalSyncICache(Page, PAGE_SIZE);
    }
    return 0;
}

int ElfMapStack(VIRTUAL_ADDRESS_SPACE *Space) {
    UINT64 Flags = PTE_PRESENT | PTE_WRITABLE | PTE_USER;
    for (UINT64 Virt = USER_STACK_VIRT;
         Virt < USER_STACK_VIRT + USER_STACK_SIZE;
         Virt += PAGE_SIZE) {
        void *Page = PhysicalMemoryAllocatePage();
        if (!Page) {
            return -1;
        }
        ElfZeroMemory(Page, PAGE_SIZE);
        if (VirtualMemorySpaceMapPage(Space, Virt, (UINT64)(UINTN)Page, Flags) != 0) {
            PhysicalMemoryFreePage(Page);
            return -1;
        }
    }
    return 0;
}

int ElfPhdrs(const UINT8 *Bytes, UINTN Size, const Elf64_Ehdr *Hdr,
             const Elf64_Phdr **OutPh, UINT16 *OutN) {
    UINT64 End;

    if (Hdr->e_phnum == 0) {
        return -1;
    }
    End = Hdr->e_phoff + (UINT64)Hdr->e_phnum * sizeof(Elf64_Phdr);
    if (Hdr->e_phoff >= Size || End > Size || End < Hdr->e_phoff) {
        return -1;
    }
    *OutPh = (const Elf64_Phdr *)(Bytes + Hdr->e_phoff);
    *OutN = Hdr->e_phnum;
    return 0;
}

/* 文件内虚拟址（未加 Bias）→ 文件偏移；仅覆盖 PT_LOAD 的 filesz 范围 */
int ElfVaToFileOff(const Elf64_Phdr *Ph, UINT16 N, UINT64 Va, UINT64 Bias,
                   UINT64 *Off, UINTN ImageSize) {
    UINT16 i;

    for (i = 0; i < N; i++) {
        UINT64 Start;
        UINT64 FileEnd;

        if (Ph[i].p_type != PT_LOAD) {
            continue;
        }
        Start = Ph[i].p_vaddr + Bias;
        FileEnd = Start + Ph[i].p_filesz;
        if (Va >= Start && Va < FileEnd) {
            UINT64 Rel = Va - Start;
            if (Ph[i].p_offset + Rel >= ImageSize) {
                return -1;
            }
            *Off = Ph[i].p_offset + Rel;
            return 0;
        }
    }
    return -1;
}

const Elf64_Dyn *ElfFindDynamic(const UINT8 *Bytes, UINTN Size,
                                const Elf64_Phdr *Ph, UINT16 N,
                                UINT64 *DynBytes) {
    UINT16 i;

    for (i = 0; i < N; i++) {
        if (Ph[i].p_type != PT_DYNAMIC) {
            continue;
        }
        if (Ph[i].p_offset >= Size || Ph[i].p_filesz == 0) {
            return 0;
        }
        if (Ph[i].p_offset + Ph[i].p_filesz > Size) {
            return 0;
        }
        *DynBytes = Ph[i].p_filesz;
        return (const Elf64_Dyn *)(Bytes + Ph[i].p_offset);
    }
    return 0;
}
