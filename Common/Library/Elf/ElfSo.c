/*
 * ElfSo.c — PR-S3-elf-1：DT_NEEDED / ET_DYN 共享库装载
 */
#include "ElfPrivate.h"
#include "ToySerialLog.h"

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

int ElfLoadShared(VIRTUAL_ADDRESS_SPACE *Space, const void *Image, UINTN Size,
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
        ToyLogMem("elf: shared not ET_DYN\n");
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
            ToyLogMem("elf: map shared segment failed\n");
            return -1;
        }
    }
    ElfZeroMemory(Info, sizeof(*Info));
    Info->Base = Base;
    Info->Image = Bytes;
    Info->Size = Size;
    if (ElfFillSoSyms(Info, Ph, Pn) != 0) {
        ToyLogMem("elf: shared dynsym failed\n");
        return -1;
    }
    return 0;
}
