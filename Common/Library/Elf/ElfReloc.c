/*
 * ElfReloc.c — PR-S3-elf-1：RELA / JMPREL 重定位
 */
#include "ElfPrivate.h"
#include "Hal.h"
#include "ToySerialLog.h"

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
            if (ElfStrEq(SymName, Name)) {
                return So->Base + Sym->st_value;
            }
        }
    }
    return 0;
}

static int ElfApplyRelaTable(VIRTUAL_ADDRESS_SPACE *Space, const UINT8 *Bytes, UINTN Size,
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

    if (RelaVa == 0 || RelaSz == 0 || RelaEnt < sizeof(Elf64_Rela)) {
        return 0;
    }
    if (ElfVaToFileOff(Ph, Pn, RelaVa, Bias, &RelaOff, Size) != 0) {
        /* EXEC 的 JMPREL 是绝对 VA，Bias=0 */
        if (Bias != 0 || ElfVaToFileOff(Ph, Pn, RelaVa, 0, &RelaOff, Size) != 0) {
            ToyLogMem("elf: rela va translate failed\n");
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
                    ToyLogMem("elf: reloc needs symtab\n");
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
                        ToyLogMem("elf: unresolved ");
                        ToyLogMem(Name);
                        ToyLogMem("\n");
                        return -1;
                    }
                    if (Kind == HAL_ELF_RELOC_ABS64) {
                        Value += (UINT64)R->r_addend;
                    }
                }
                break;
            case HAL_ELF_RELOC_COPY:
                ToyLogMem("elf: COPY reloc unsupported\n");
                return -1;
            default:
                ToyLogMem("elf: unsupported reloc\n");
                return -1;
            }
        }

        if (VirtualMemoryCopyToSpace(Space, Dest, &Value, sizeof(Value)) < 0) {
            ToyLogMem("elf: reloc write failed\n");
            return -1;
        }
    }
    return 0;
}

int ElfRelocateProgram(VIRTUAL_ADDRESS_SPACE *Space, const void *Image, UINTN Size,
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
