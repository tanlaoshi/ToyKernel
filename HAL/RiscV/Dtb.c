/*
 * Dtb.c — 最小 Flattened Device Tree：找 memory 节点的 reg（PR-V1）
 * big-endian 属性；支持 #address-cells/#size-cells 为 1 或 2。
 */
#include "Dtb.h"

#define FDT_MAGIC       0xd00dfeedu
#define FDT_BEGIN_NODE  0x1u
#define FDT_END_NODE    0x2u
#define FDT_PROP        0x3u
#define FDT_NOP         0x4u
#define FDT_END         0x9u

typedef struct {
    UINT32 Magic;
    UINT32 Totalsize;
    UINT32 OffDtStruct;
    UINT32 OffDtStrings;
    UINT32 OffMemRsvmap;
    UINT32 Version;
    UINT32 LastCompVersion;
    UINT32 BootCpuidPhys;
    UINT32 SizeDtStrings;
    UINT32 SizeDtStruct;
} FDT_HEADER;

static UINT32 Be32(const void *P) {
    const UINT8 *B = (const UINT8 *)P;
    return ((UINT32)B[0] << 24) | ((UINT32)B[1] << 16) |
           ((UINT32)B[2] << 8) | (UINT32)B[3];
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

static int NameIsMemory(const char *Name) {
    /* "memory" 或 "memory@..." */
    if (!Name) {
        return 0;
    }
    if (Name[0] != 'm' || Name[1] != 'e' || Name[2] != 'm' ||
        Name[3] != 'o' || Name[4] != 'r' || Name[5] != 'y') {
        return 0;
    }
    return Name[6] == 0 || Name[6] == '@';
}

static UINT32 Align4(UINT32 Off) {
    return (Off + 3u) & ~3u;
}

int DtbMemoryRegion(UINT64 DtbPhys, UINT64 *OutBase, UINT64 *OutSize) {
    const UINT8 *Blob;
    const FDT_HEADER *Hdr;
    UINT32 Total;
    UINT32 StructOff;
    UINT32 StructSize;
    UINT32 StringsOff;
    UINT32 Off;
    UINT32 End;
    UINT32 Depth;
    UINT32 AddrCells;
    UINT32 SizeCells;
    int InMemory;
    UINT64 MemBase;
    UINT64 MemSize;
    int HaveMem;

    if (!OutBase || !OutSize || DtbPhys == 0) {
        return -1;
    }
    Blob = (const UINT8 *)(UINTN)DtbPhys;
    Hdr = (const FDT_HEADER *)Blob;
    if (Be32(&Hdr->Magic) != FDT_MAGIC) {
        return -1;
    }
    Total = Be32(&Hdr->Totalsize);
    StructOff = Be32(&Hdr->OffDtStruct);
    StructSize = Be32(&Hdr->SizeDtStruct);
    StringsOff = Be32(&Hdr->OffDtStrings);
    if (Total < sizeof(FDT_HEADER) || StructOff >= Total ||
        StringsOff >= Total || StructSize == 0 ||
        StructOff + StructSize > Total) {
        return -1;
    }

    AddrCells = 2;
    SizeCells = 1;
    Depth = 0;
    InMemory = 0;
    HaveMem = 0;
    MemBase = 0;
    MemSize = 0;
    Off = StructOff;
    End = StructOff + StructSize;

    while (Off + 4 <= End) {
        UINT32 Token = Be32(Blob + Off);
        Off += 4;

        if (Token == FDT_BEGIN_NODE) {
            const char *Name = (const char *)(Blob + Off);
            UINT32 Len = 0;
            while (Off + Len < End && Name[Len]) {
                Len++;
            }
            Off = Align4(Off + Len + 1);
            Depth++;
            /* QEMU virt：memory@… 在根下；也接受名恰为 memory */
            InMemory = NameIsMemory(Name) ? 1 : 0;
            continue;
        }
        if (Token == FDT_END_NODE) {
            if (Depth > 0) {
                Depth--;
            }
            InMemory = 0;
            continue;
        }
        if (Token == FDT_NOP) {
            continue;
        }
        if (Token == FDT_END) {
            break;
        }
        if (Token != FDT_PROP) {
            return -1;
        }
        {
            UINT32 PropLen;
            UINT32 NameOff;
            const char *PName;
            const UINT8 *Val;

            if (Off + 8 > End) {
                return -1;
            }
            PropLen = Be32(Blob + Off);
            NameOff = Be32(Blob + Off + 4);
            Off += 8;
            if (StringsOff + NameOff >= Total || Off + PropLen > End) {
                return -1;
            }
            PName = (const char *)(Blob + StringsOff + NameOff);
            Val = Blob + Off;
            Off = Align4(Off + PropLen);

            /* 根节点上的 #address-cells / #size-cells（Depth==1） */
            if (Depth == 1 && !InMemory && StrEq(PName, "#address-cells") &&
                PropLen >= 4) {
                AddrCells = Be32(Val);
            } else if (Depth == 1 && !InMemory && StrEq(PName, "#size-cells") &&
                       PropLen >= 4) {
                SizeCells = Be32(Val);
            } else if (InMemory && StrEq(PName, "reg") && !HaveMem) {
                UINT32 Need = (AddrCells + SizeCells) * 4u;
                UINT64 Base = 0;
                UINT64 Size = 0;
                UINT32 i;
                UINT32 P = 0;

                if (PropLen < Need || AddrCells == 0 || AddrCells > 2 ||
                    SizeCells == 0 || SizeCells > 2) {
                    continue;
                }
                for (i = 0; i < AddrCells; i++) {
                    Base = (Base << 32) | (UINT64)Be32(Val + P);
                    P += 4;
                }
                for (i = 0; i < SizeCells; i++) {
                    Size = (Size << 32) | (UINT64)Be32(Val + P);
                    P += 4;
                }
                if (Size == 0) {
                    continue;
                }
                MemBase = Base;
                MemSize = Size;
                HaveMem = 1;
            }
        }
    }

    if (!HaveMem) {
        return -1;
    }
    *OutBase = MemBase;
    *OutSize = MemSize;
    return 0;
}

static int NameIsFwCfg(const char *Name) {
    /* "fw-cfg" 或 "fw-cfg@..." */
    if (!Name) {
        return 0;
    }
    if (Name[0] != 'f' || Name[1] != 'w' || Name[2] != '-' ||
        Name[3] != 'c' || Name[4] != 'f' || Name[5] != 'g') {
        return 0;
    }
    return Name[6] == 0 || Name[6] == '@';
}

int DtbFwCfgBase(UINT64 DtbPhys, UINT64 *OutBase) {
    const UINT8 *Blob;
    const FDT_HEADER *Hdr;
    UINT32 Total;
    UINT32 StructOff;
    UINT32 StructSize;
    UINT32 StringsOff;
    UINT32 Off;
    UINT32 End;
    UINT32 Depth;
    UINT32 AddrCells;
    UINT32 SizeCells;
    int InFwCfg;
    int Have;
    UINT64 Base;

    if (!OutBase || DtbPhys == 0) {
        return -1;
    }
    Blob = (const UINT8 *)(UINTN)DtbPhys;
    Hdr = (const FDT_HEADER *)Blob;
    if (Be32(&Hdr->Magic) != FDT_MAGIC) {
        return -1;
    }
    Total = Be32(&Hdr->Totalsize);
    StructOff = Be32(&Hdr->OffDtStruct);
    StructSize = Be32(&Hdr->SizeDtStruct);
    StringsOff = Be32(&Hdr->OffDtStrings);
    if (Total < sizeof(FDT_HEADER) || StructOff >= Total ||
        StringsOff >= Total || StructSize == 0 ||
        StructOff + StructSize > Total) {
        return -1;
    }

    AddrCells = 2;
    SizeCells = 1;
    Depth = 0;
    InFwCfg = 0;
    Have = 0;
    Base = 0;
    Off = StructOff;
    End = StructOff + StructSize;

    while (Off + 4 <= End) {
        UINT32 Token = Be32(Blob + Off);
        Off += 4;

        if (Token == FDT_BEGIN_NODE) {
            const char *Name = (const char *)(Blob + Off);
            UINT32 Len = 0;
            while (Off + Len < End && Name[Len]) {
                Len++;
            }
            Off = Align4(Off + Len + 1);
            Depth++;
            InFwCfg = NameIsFwCfg(Name) ? 1 : 0;
            continue;
        }
        if (Token == FDT_END_NODE) {
            if (Depth > 0) {
                Depth--;
            }
            InFwCfg = 0;
            continue;
        }
        if (Token == FDT_NOP) {
            continue;
        }
        if (Token == FDT_END) {
            break;
        }
        if (Token != FDT_PROP) {
            return -1;
        }
        {
            UINT32 PropLen;
            UINT32 NameOff;
            const char *PName;
            const UINT8 *Val;

            if (Off + 8 > End) {
                return -1;
            }
            PropLen = Be32(Blob + Off);
            NameOff = Be32(Blob + Off + 4);
            Off += 8;
            if (StringsOff + NameOff >= Total || Off + PropLen > End) {
                return -1;
            }
            PName = (const char *)(Blob + StringsOff + NameOff);
            Val = Blob + Off;
            Off = Align4(Off + PropLen);

            if (Depth == 1 && !InFwCfg && StrEq(PName, "#address-cells") &&
                PropLen >= 4) {
                AddrCells = Be32(Val);
            } else if (Depth == 1 && !InFwCfg && StrEq(PName, "#size-cells") &&
                       PropLen >= 4) {
                SizeCells = Be32(Val);
            } else if (InFwCfg && StrEq(PName, "reg") && !Have) {
                UINT32 Need = (AddrCells + SizeCells) * 4u;
                UINT64 B = 0;
                UINT64 Sz = 0;
                UINT32 i;
                UINT32 P = 0;

                if (PropLen < Need || AddrCells == 0 || AddrCells > 2 ||
                    SizeCells == 0 || SizeCells > 2) {
                    continue;
                }
                for (i = 0; i < AddrCells; i++) {
                    B = (B << 32) | (UINT64)Be32(Val + P);
                    P += 4;
                }
                for (i = 0; i < SizeCells; i++) {
                    Sz = (Sz << 32) | (UINT64)Be32(Val + P);
                    P += 4;
                }
                (void)Sz;
                Base = B;
                Have = 1;
            }
        }
    }

    if (!Have) {
        return -1;
    }
    *OutBase = Base;
    return 0;
}
