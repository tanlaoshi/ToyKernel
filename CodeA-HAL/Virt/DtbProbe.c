/*
 * DtbProbe.c — FDT fw-cfg 基址与 cpu@ 计数（PR-S3-dtb-1）
 */
#include "Dtb.h"
#include "DtbPrivate.h"

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
    InFwCfg = 0;
    Have = 0;
    Base = 0;
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
            PropLen = DtbBe32(Blob + Off);
            NameOff = DtbBe32(Blob + Off + 4);
            Off += 8;
            if (StringsOff + NameOff >= Total || Off + PropLen > End) {
                return -1;
            }
            PName = (const char *)(Blob + StringsOff + NameOff);
            Val = Blob + Off;
            Off = DtbAlign4(Off + PropLen);

            if (Depth == 1 && !InFwCfg && DtbStrEq(PName, "#address-cells") &&
                PropLen >= 4) {
                AddrCells = DtbBe32(Val);
            } else if (Depth == 1 && !InFwCfg && DtbStrEq(PName, "#size-cells") &&
                       PropLen >= 4) {
                SizeCells = DtbBe32(Val);
            } else if (InFwCfg && DtbStrEq(PName, "reg") && !Have) {
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
                    B = (B << 32) | (UINT64)DtbBe32(Val + P);
                    P += 4;
                }
                for (i = 0; i < SizeCells; i++) {
                    Sz = (Sz << 32) | (UINT64)DtbBe32(Val + P);
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

static int NameIsCpuAt(const char *Name) {
    if (!Name || Name[0] != 'c' || Name[1] != 'p' || Name[2] != 'u' ||
        Name[3] != '@') {
        return 0;
    }
    return 1;
}

int DtbCpuCount(UINT64 DtbPhys) {
    const UINT8 *Blob;
    const FDT_HEADER *Hdr;
    UINT32 Total;
    UINT32 StructOff;
    UINT32 StructSize;
    UINT32 Off;
    UINT32 End;
    int Count;

    if (DtbPhys == 0) {
        return 1;
    }
    Blob = (const UINT8 *)(UINTN)DtbPhys;
    Hdr = (const FDT_HEADER *)Blob;
    if (DtbBe32(&Hdr->Magic) != FDT_MAGIC) {
        return 1;
    }
    Total = DtbBe32(&Hdr->Totalsize);
    StructOff = DtbBe32(&Hdr->OffDtStruct);
    StructSize = DtbBe32(&Hdr->SizeDtStruct);
    if (Total < sizeof(FDT_HEADER) || StructOff >= Total || StructSize == 0 ||
        StructOff + StructSize > Total) {
        return 1;
    }

    Count = 0;
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
            if (NameIsCpuAt(Name)) {
                Count++;
            }
            continue;
        }
        if (Token == FDT_END_NODE || Token == FDT_NOP) {
            continue;
        }
        if (Token == FDT_END) {
            break;
        }
        if (Token != FDT_PROP) {
            break;
        }
        {
            UINT32 PropLen;
            if (Off + 8 > End) {
                break;
            }
            PropLen = DtbBe32(Blob + Off);
            Off += 8;
            Off = DtbAlign4(Off + PropLen);
        }
    }
    return Count > 0 ? Count : 1;
}
