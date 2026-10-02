/*
 * Dtb.c — FDT memory 节点 reg（PR-S3-dtb-1）
 *
 * fw-cfg / cpu@ 见 DtbProbe.c。
 */
#include "Dtb.h"
#include "DtbPrivate.h"

UINT32 DtbBe32(const void *P) {
    const UINT8 *B = (const UINT8 *)P;
    return ((UINT32)B[0] << 24) | ((UINT32)B[1] << 16) |
           ((UINT32)B[2] << 8) | (UINT32)B[3];
}

int DtbStrEq(const char *A, const char *B) {
    if (!A || !B) {
        return 0;
    }
    while (*A && *A == *B) {
        A++;
        B++;
    }
    return *A == *B;
}

UINT32 DtbAlign4(UINT32 Off) {
    return (Off + 3u) & ~3u;
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
    if (DtbBe32(&Hdr->Magic) != FDT_MAGIC) {
        return -1;
    }
    Total = DtbBe32(&Hdr->Totalsize);
    StructOff = DtbBe32(&Hdr->OffDtStruct);
    StructSize = DtbBe32(&Hdr->SizeDtStruct);
    StringsOff = DtbBe32(&Hdr->OffDtStrings);
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
        UINT32 Token = DtbBe32(Blob + Off);
        Off += 4;

        if (Token == FDT_BEGIN_NODE) {
            const char *Name = (const char *)(Blob + Off);
            UINT32 Len = 0;
            while (Off + Len < End && Name[Len]) {
                Len++;
            }
            Off = DtbAlign4(Off + Len + 1);
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
            PropLen = DtbBe32(Blob + Off);
            NameOff = DtbBe32(Blob + Off + 4);
            Off += 8;
            if (StringsOff + NameOff >= Total || Off + PropLen > End) {
                return -1;
            }
            PName = (const char *)(Blob + StringsOff + NameOff);
            Val = Blob + Off;
            Off = DtbAlign4(Off + PropLen);

            /* 根节点上的 #address-cells / #size-cells（Depth==1） */
            if (Depth == 1 && !InMemory && DtbStrEq(PName, "#address-cells") &&
                PropLen >= 4) {
                AddrCells = DtbBe32(Val);
            } else if (Depth == 1 && !InMemory && DtbStrEq(PName, "#size-cells") &&
                       PropLen >= 4) {
                SizeCells = DtbBe32(Val);
            } else if (InMemory && DtbStrEq(PName, "reg") && !HaveMem) {
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
                    Base = (Base << 32) | (UINT64)DtbBe32(Val + P);
                    P += 4;
                }
                for (i = 0; i < SizeCells; i++) {
                    Size = (Size << 32) | (UINT64)DtbBe32(Val + P);
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

