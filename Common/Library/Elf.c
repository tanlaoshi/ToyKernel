/*
 * Elf.c — ELF64 加载：静态 ET_EXEC + 简易动态链接（DT_NEEDED / JUMP_SLOT）
 */
#include "Elf.h"
#include "PhysicalMemory.h"
#include "Console.h"
#include "Hal.h"

static void MemZero(void *Ptr, UINTN Size) {
    UINT8 *B = (UINT8 *)Ptr;
    for (UINTN i = 0; i < Size; i++) {
        B[i] = 0;
    }
}

static int StrEq(const char *A, const char *B) {
    if (!A || !B) {
        return 0;
    }
    while (*A && *A == *B) {
        A++;
        B++;
    }
    return *A == *B;
}

static int ElfHeaderOk(const Elf64_Ehdr *Hdr, UINTN Size, UINT16 WantType) {
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

static UINT64 ElfAlignUp(UINT64 Value, UINT64 Align) {
    if (Align <= 1) {
        return Value;
    }
    return (Value + Align - 1) & ~(Align - 1);
}

static int ElfMapSegment(VM_ADDR_SPACE *Space, const UINT8 *Image,
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
        MemZero(Page, PAGE_SIZE);

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
    }
    return 0;
}

static int ElfMapStack(VM_ADDR_SPACE *Space) {
    UINT64 Flags = PTE_PRESENT | PTE_WRITABLE | PTE_USER;
    for (UINT64 Virt = USER_STACK_VIRT;
         Virt < USER_STACK_VIRT + USER_STACK_SIZE;
         Virt += PAGE_SIZE) {
        void *Page = PhysicalMemoryAllocatePage();
        if (!Page) {
            return -1;
        }
        MemZero(Page, PAGE_SIZE);
        if (VirtualMemorySpaceMapPage(Space, Virt, (UINT64)(UINTN)Page, Flags) != 0) {
            PhysicalMemoryFreePage(Page);
            return -1;
        }
    }
    return 0;
}

static int ElfPhdrs(const UINT8 *Bytes, UINTN Size, const Elf64_Ehdr *Hdr,
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
static int ElfVaToFileOff(const Elf64_Phdr *Ph, UINT16 N, UINT64 Va, UINT64 Bias,
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

static const Elf64_Dyn *ElfFindDynamic(const UINT8 *Bytes, UINTN Size,
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

static void ElfCopyShortName(char *Dst, const char *Src, UINTN Max) {
    UINTN i;

    if (Max == 0) {
        return;
    }
    for (i = 0; i + 1 < Max && Src[i]; i++) {
        char C = Src[i];
        if (C >= 'a' && C <= 'z') {
            C = (char)(C - 'a' + 'A');
        }
        Dst[i] = C;
    }
    Dst[i] = 0;
}

int ElfCollectNeeded(const void *Image, UINTN Size, char Names[][16], int Max) {
    const UINT8 *Bytes = (const UINT8 *)Image;
    const Elf64_Ehdr *Hdr = (const Elf64_Ehdr *)Image;
    const Elf64_Phdr *Ph;
    UINT16 Pn;
    const Elf64_Dyn *Dyn;
    UINT64 DynBytes;
    UINT64 StrVa = 0;
    UINT64 StrOff = 0;
    int Count = 0;
    UINTN i;
    UINTN NEnt;

    if (!Image || !Names || Max <= 0) {
        return -1;
    }
    if (!ElfHeaderOk(Hdr, Size, ET_EXEC) && !ElfHeaderOk(Hdr, Size, ET_DYN)) {
        return -1;
    }
    if (ElfPhdrs(Bytes, Size, Hdr, &Ph, &Pn) != 0) {
        return -1;
    }
    Dyn = ElfFindDynamic(Bytes, Size, Ph, Pn, &DynBytes);
    if (!Dyn) {
        return 0;
    }
    NEnt = (UINTN)(DynBytes / sizeof(Elf64_Dyn));
    for (i = 0; i < NEnt; i++) {
        if (Dyn[i].d_tag == DT_STRTAB) {
            StrVa = Dyn[i].d_un.d_ptr;
        }
    }
    if (StrVa == 0) {
        return 0;
    }
    /* EXEC：STRTAB 已是绝对 VA；DYN：通常为未加基址的 vaddr */
    if (ElfVaToFileOff(Ph, Pn, StrVa, 0, &StrOff, Size) != 0) {
        return -1;
    }
    for (i = 0; i < NEnt; i++) {
        const char *Name;
        UINT64 Off;

        if (Dyn[i].d_tag != DT_NEEDED) {
            continue;
        }
        if (Count >= Max) {
            break;
        }
        Off = StrOff + Dyn[i].d_un.d_val;
        if (Off >= Size) {
            return -1;
        }
        Name = (const char *)(Bytes + Off);
        ElfCopyShortName(Names[Count], Name, 16);
        if (Names[Count][0] == 0) {
            continue;
        }
        Count++;
    }
    return Count;
}

static int ElfFillSoSyms(ELF_SO_INFO *Info, const Elf64_Phdr *Ph, UINT16 Pn) {
    const Elf64_Dyn *Dyn;
    UINT64 DynBytes;
    UINT64 SymVa = 0;
    UINT64 StrVa = 0;
    UINT64 SymEnt = sizeof(Elf64_Sym);
    UINT64 HashVa = 0;
    UINTN i;
    UINTN NEnt;
    UINT64 SymOff;
    UINT64 StrOff;
    UINT64 SymBytes = 0;

    Dyn = ElfFindDynamic(Info->Image, Info->Size, Ph, Pn, &DynBytes);
    if (!Dyn) {
        return -1;
    }
    NEnt = (UINTN)(DynBytes / sizeof(Elf64_Dyn));
    for (i = 0; i < NEnt; i++) {
        switch (Dyn[i].d_tag) {
        case DT_SYMTAB:
            SymVa = Dyn[i].d_un.d_ptr;
            break;
        case DT_STRTAB:
            StrVa = Dyn[i].d_un.d_ptr;
            break;
        case DT_SYMENT:
            SymEnt = Dyn[i].d_un.d_val;
            break;
        case DT_HASH:
            HashVa = Dyn[i].d_un.d_ptr;
            break;
        default:
            break;
        }
    }
    if (SymVa == 0 || StrVa == 0 || SymEnt == 0) {
        return -1;
    }
    if (ElfVaToFileOff(Ph, Pn, SymVa, 0, &SymOff, Info->Size) != 0 ||
        ElfVaToFileOff(Ph, Pn, StrVa, 0, &StrOff, Info->Size) != 0) {
        return -1;
    }
    if (HashVa != 0) {
        UINT64 HashOff;
        const UINT32 *Hash;

        if (ElfVaToFileOff(Ph, Pn, HashVa, 0, &HashOff, Info->Size) == 0 &&
            HashOff + 8 <= Info->Size) {
            Hash = (const UINT32 *)(Info->Image + HashOff);
            /* nchain = number of dynsym entries */
            Info->DynSymCount = Hash[1];
        }
    }
    if (Info->DynSymCount == 0) {
        /* 无 HASH 时保守扫到文件尾（受 Image 限制） */
        SymBytes = Info->Size - SymOff;
        Info->DynSymCount = (UINTN)(SymBytes / SymEnt);
        if (Info->DynSymCount > 64) {
            Info->DynSymCount = 64;
        }
    }
    Info->DynSym = (const Elf64_Sym *)(Info->Image + SymOff);
    Info->DynStr = (const char *)(Info->Image + StrOff);
    (void)SymEnt;
    return 0;
}

int ElfLoadShared(VM_ADDR_SPACE *Space, const void *Image, UINTN Size,
                  UINT64 Base, ELF_SO_INFO *Info) {
    const UINT8 *Bytes = (const UINT8 *)Image;
    const Elf64_Ehdr *Hdr = (const Elf64_Ehdr *)Image;
    const Elf64_Phdr *Ph;
    UINT16 Pn;
    UINT16 i;

    if (!Space || !Image || !Info) {
        return -1;
    }
    if (!ElfHeaderOk(Hdr, Size, ET_DYN)) {
        ConsoleWrite("elf: shared not ET_DYN\n");
        return -1;
    }
    if (ElfPhdrs(Bytes, Size, Hdr, &Ph, &Pn) != 0) {
        return -1;
    }
    for (i = 0; i < Pn; i++) {
        if (Ph[i].p_type != PT_LOAD) {
            continue;
        }
        if (ElfMapSegment(Space, Bytes, &Ph[i], Base) != 0) {
            ConsoleWrite("elf: map shared segment failed\n");
            return -1;
        }
    }
    MemZero(Info, sizeof(*Info));
    Info->Base = Base;
    Info->Image = Bytes;
    Info->Size = Size;
    if (ElfFillSoSyms(Info, Ph, Pn) != 0) {
        ConsoleWrite("elf: shared dynsym failed\n");
        return -1;
    }
    return 0;
}

static UINT64 ElfLookupSymbol(const ELF_SO_INFO *Sos, int SoCount,
                              const char *Name) {
    int S;
    UINTN I;

    if (!Name || !Name[0]) {
        return 0;
    }
    for (S = 0; S < SoCount; S++) {
        const ELF_SO_INFO *So = &Sos[S];
        if (!So->DynSym || !So->DynStr) {
            continue;
        }
        for (I = 0; I < So->DynSymCount; I++) {
            const Elf64_Sym *Sym = &So->DynSym[I];
            const char *SymName;
            UINT8 Bind = (UINT8)(Sym->st_info >> 4);
            UINT8 Type = (UINT8)(Sym->st_info & 0xF);

            if (Sym->st_name == 0 || Sym->st_shndx == 0) {
                continue;
            }
            if (Bind != STB_GLOBAL) {
                continue;
            }
            if (Type != STT_FUNC && Type != STT_OBJECT && Type != STT_NOTYPE) {
                continue;
            }
            SymName = So->DynStr + Sym->st_name;
            if (StrEq(SymName, Name)) {
                return So->Base + Sym->st_value;
            }
        }
    }
    return 0;
}

static int ElfApplyRelaTable(VM_ADDR_SPACE *Space, const UINT8 *Bytes, UINTN Size,
                             const Elf64_Phdr *Ph, UINT16 Pn, UINT64 Bias,
                             UINT64 RelaVa, UINT64 RelaSz, UINT64 RelaEnt,
                             UINT64 SymVa, UINT64 StrVa, UINT64 SymEnt,
                             const ELF_SO_INFO *Sos, int SoCount) {
    UINT64 RelaOff;
    UINT64 SymOff = 0;
    UINT64 StrOff = 0;
    UINTN Count;
    UINTN i;
    int HaveSym = 0;

    (void)Space;

    if (RelaVa == 0 || RelaSz == 0 || RelaEnt < sizeof(Elf64_Rela)) {
        return 0;
    }
    if (ElfVaToFileOff(Ph, Pn, RelaVa, Bias, &RelaOff, Size) != 0) {
        /* EXEC 的 JMPREL 是绝对 VA，Bias=0 */
        if (Bias != 0 || ElfVaToFileOff(Ph, Pn, RelaVa, 0, &RelaOff, Size) != 0) {
            ConsoleWrite("elf: rela va translate failed\n");
            return -1;
        }
    }
    if (SymVa != 0 && StrVa != 0 && SymEnt != 0) {
        if (ElfVaToFileOff(Ph, Pn, SymVa, Bias, &SymOff, Size) == 0 &&
            ElfVaToFileOff(Ph, Pn, StrVa, Bias, &StrOff, Size) == 0) {
            HaveSym = 1;
        } else if (Bias == 0 &&
                   ElfVaToFileOff(Ph, Pn, SymVa, 0, &SymOff, Size) == 0 &&
                   ElfVaToFileOff(Ph, Pn, StrVa, 0, &StrOff, Size) == 0) {
            HaveSym = 1;
        }
    }
    Count = (UINTN)(RelaSz / RelaEnt);
    for (i = 0; i < Count; i++) {
        const Elf64_Rela *R;
        UINT32 Type;
        UINT32 SymIdx;
        UINT64 Value = 0;
        UINT64 Dest;

        if (RelaOff + (i + 1) * RelaEnt > Size) {
            return -1;
        }
        R = (const Elf64_Rela *)(Bytes + RelaOff + i * RelaEnt);
        Type = ELF_R_TYPE(R->r_info);
        SymIdx = (UINT32)ELF_R_SYM(R->r_info);
        Dest = R->r_offset + Bias;

        {
            HAL_ELF_RELOC_KIND Kind = HalElfRelocKind(Type);

            switch (Kind) {
            case HAL_ELF_RELOC_RELATIVE:
                Value = Bias + (UINT64)R->r_addend;
                break;
            case HAL_ELF_RELOC_JUMP_SLOT:
            case HAL_ELF_RELOC_GLOB_DAT:
            case HAL_ELF_RELOC_ABS64:
                if (!HaveSym) {
                    ConsoleWrite("elf: reloc needs symtab\n");
                    return -1;
                }
                {
                    const Elf64_Sym *Sym;
                    const char *Name;

                    if (SymOff + (SymIdx + 1) * SymEnt > Size) {
                        return -1;
                    }
                    Sym = (const Elf64_Sym *)(Bytes + SymOff + SymIdx * SymEnt);
                    Name = (const char *)(Bytes + StrOff + Sym->st_name);
                    Value = ElfLookupSymbol(Sos, SoCount, Name);
                    if (Value == 0) {
                        ConsoleWrite("elf: unresolved ");
                        ConsoleWrite(Name);
                        ConsoleWrite("\n");
                        return -1;
                    }
                    if (Kind == HAL_ELF_RELOC_ABS64) {
                        Value += (UINT64)R->r_addend;
                    }
                }
                break;
            case HAL_ELF_RELOC_COPY:
                ConsoleWrite("elf: COPY reloc unsupported\n");
                return -1;
            default:
                ConsoleWrite("elf: unsupported reloc\n");
                return -1;
            }
        }

        if (VirtualMemoryCopyToUser(Dest, &Value, sizeof(Value)) < 0) {
            ConsoleWrite("elf: reloc write failed\n");
            return -1;
        }
    }
    return 0;
}

int ElfRelocateProgram(VM_ADDR_SPACE *Space, const void *Image, UINTN Size,
                       const ELF_SO_INFO *Sos, int SoCount) {
    const UINT8 *Bytes = (const UINT8 *)Image;
    const Elf64_Ehdr *Hdr = (const Elf64_Ehdr *)Image;
    const Elf64_Phdr *Ph;
    UINT16 Pn;
    const Elf64_Dyn *Dyn;
    UINT64 DynBytes;
    UINT64 RelaVa = 0;
    UINT64 RelaSz = 0;
    UINT64 RelaEnt = sizeof(Elf64_Rela);
    UINT64 JmpVa = 0;
    UINT64 PltRelSz = 0;
    UINT64 SymVa = 0;
    UINT64 StrVa = 0;
    UINT64 SymEnt = sizeof(Elf64_Sym);
    UINTN i;
    UINTN NEnt;
    UINT64 Bias = 0;

    if (!Space || !Image) {
        return -1;
    }
    if (!ElfHeaderOk(Hdr, Size, ET_EXEC) && !ElfHeaderOk(Hdr, Size, ET_DYN)) {
        return -1;
    }
    if (ElfPhdrs(Bytes, Size, Hdr, &Ph, &Pn) != 0) {
        return -1;
    }
    Dyn = ElfFindDynamic(Bytes, Size, Ph, Pn, &DynBytes);
    if (!Dyn) {
        return 0;
    }
    NEnt = (UINTN)(DynBytes / sizeof(Elf64_Dyn));
    for (i = 0; i < NEnt; i++) {
        switch (Dyn[i].d_tag) {
        case DT_RELA:
            RelaVa = Dyn[i].d_un.d_ptr;
            break;
        case DT_RELASZ:
            RelaSz = Dyn[i].d_un.d_val;
            break;
        case DT_RELAENT:
            RelaEnt = Dyn[i].d_un.d_val;
            break;
        case DT_JMPREL:
            JmpVa = Dyn[i].d_un.d_ptr;
            break;
        case DT_PLTRELSZ:
            PltRelSz = Dyn[i].d_un.d_val;
            break;
        case DT_SYMTAB:
            SymVa = Dyn[i].d_un.d_ptr;
            break;
        case DT_STRTAB:
            StrVa = Dyn[i].d_un.d_ptr;
            break;
        case DT_SYMENT:
            SymEnt = Dyn[i].d_un.d_val;
            break;
        default:
            break;
        }
    }

    if (ElfApplyRelaTable(Space, Bytes, Size, Ph, Pn, Bias,
                          RelaVa, RelaSz, RelaEnt,
                          SymVa, StrVa, SymEnt, Sos, SoCount) != 0) {
        return -1;
    }
    if (ElfApplyRelaTable(Space, Bytes, Size, Ph, Pn, Bias,
                          JmpVa, PltRelSz, RelaEnt,
                          SymVa, StrVa, SymEnt, Sos, SoCount) != 0) {
        return -1;
    }
    return 0;
}

int ElfLoadFromMemory(VM_ADDR_SPACE *Space, const void *Image, UINTN Size,
                      ELF_LOAD_RESULT *Out) {
    const UINT8 *Bytes = (const UINT8 *)Image;
    const Elf64_Ehdr *Hdr = (const Elf64_Ehdr *)Image;
    const Elf64_Phdr *Phdrs;
    UINT16 Pn;
    UINT16 i;
    UINT64 BrkBase = USER_CODE_VIRT;

    if (!ElfHeaderOk(Hdr, Size, ET_EXEC)) {
        ConsoleWrite("elf: bad header\n");
        return -1;
    }
    if (ElfPhdrs(Bytes, Size, Hdr, &Phdrs, &Pn) != 0) {
        ConsoleWrite("elf: phdr out of range\n");
        return -1;
    }

    for (i = 0; i < Pn; i++) {
        UINT64 SegEnd;

        if (Phdrs[i].p_type != PT_LOAD) {
            continue;
        }
        if (ElfMapSegment(Space, Bytes, &Phdrs[i], 0) != 0) {
            ConsoleWrite("elf: map segment failed\n");
            return -1;
        }
        SegEnd = Phdrs[i].p_vaddr + Phdrs[i].p_memsz;
        if (SegEnd > BrkBase) {
            BrkBase = SegEnd;
        }
    }

    if (ElfMapStack(Space) != 0) {
        ConsoleWrite("elf: map stack failed\n");
        return -1;
    }

    if (Out) {
        Out->Entry = Hdr->e_entry;
        Out->StackTop = USER_STACK_VIRT + USER_STACK_SIZE;
        Out->BrkBase = BrkBase;
    }
    return 0;
}
