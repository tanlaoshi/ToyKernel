/*
 * FatDirName.c — 目录名匹配与 8.3 别名（PR-S-fatdir-1）
 */
#include "Fat.h"
#include "FatPrivate.h"
#include "Block.h"
#include "LibWrite.h"

int NameIsDot(const UINT8 *E) {
    return E[0] == '.' && (E[1] == ' ' || (E[1] == '.' && E[2] == ' '));
}
/*
 * 查找 Name 对应 SFN 下标，或 Need 个连续空闲槽起点。
 * 找到已存在时 *FoundExisting=1 且 *OutIndex 为 SFN；否则为第一空闲槽。
 *
 * 必须先扫完全表再认空闲：若中途遇 E5 就 return，后面的同名活项会漏掉，
 * store remove 表现为 NOENT + DirHasFileCI 仍在（file remains）。
 */
int FindDirIndex(FAT_DIR_CTX Dir, const char *Name, int Need,
                        int *OutIndex, int *FoundExisting,
                        UINT32 *OldCluster, UINT8 *OldAttr) {
    FAT_LFN_ACC Acc;
    UINT8 E[32];
    int Max = DirMaxIndex(Dir);
    int i;
    int FreeRun = 0;
    int FreeStart = 0;
    int BestFree = -1;

    *FoundExisting = 0;
    *OldCluster = 0;
    if (OldAttr) {
        *OldAttr = 0;
    }
    if (Need < 1) {
        Need = 1;
    }
    LfnAccClear(&Acc);

    for (i = 0; i < Max; i++) {
        if (!DirReadEntry(Dir, (UINT32)i, E)) {
            return 0;
        }
        if (E[0] == 0x00) {
            if (FreeRun == 0) {
                FreeStart = i;
            }
            /* 目录尾：从 FreeStart 到表末均可用 */
            if (Max - FreeStart >= Need) {
                *OutIndex = FreeStart;
                return 1;
            }
            if (BestFree >= 0) {
                *OutIndex = BestFree;
                return 1;
            }
            return 0;
        }
        if (E[0] == 0xE5) {
            LfnAccClear(&Acc);
            if (FreeRun == 0) {
                FreeStart = i;
            }
            FreeRun++;
            if (FreeRun >= Need && BestFree < 0) {
                BestFree = FreeStart;
            }
            continue;
        }
        FreeRun = 0;
        if (EntryIsLfn(E)) {
            LfnFeed(&Acc, E);
            continue;
        }
        if (EntryIsVol(E) || NameIsDot(E)) {
            LfnAccClear(&Acc);
            continue;
        }
        if (EntryMatchesName(E, &Acc, Name)) {
            *FoundExisting = 1;
            *OutIndex = i;
            *OldCluster = EntryCluster(E);
            if (OldAttr) {
                *OldAttr = E[11];
            }
            return 1;
        }
        LfnAccClear(&Acc);
    }
    if (BestFree >= 0) {
        *OutIndex = BestFree;
        return 1;
    }
    return 0;
}

int LfnEntryCountForName(const char *Name) {
    int Len = 0;
    while (Name[Len]) {
        Len++;
    }
    if (Len == 0) {
        return 0;
    }
    return (Len + 12) / 13;
}

void FillLfnEntry(UINT8 *E, int Ord, int IsLast, UINT8 Cksum, const char *Name) {
    static const int Offs[13] = {
        1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30
    };
    int Base = (Ord - 1) * 13;
    int k;
    int Ended = 0;

    for (k = 0; k < 32; k++) {
        E[k] = 0;
    }
    E[0] = (UINT8)(Ord | (IsLast ? 0x40 : 0));
    E[11] = FAT_ATTR_LFN;
    E[13] = Cksum;
    for (k = 0; k < 13; k++) {
        UINT16 U;
        if (Ended) {
            U = 0xFFFF;
        } else if (Name[Base + k] == 0) {
            U = 0;
            Ended = 1;
        } else {
            U = (UINT8)Name[Base + k];
        }
        Write16(E + Offs[k], U);
    }
}

/* 生成与长名对应的 8.3 别名 BASE~N.EXT */
int Make83Alias(FAT_DIR_CTX Dir, const char *LongName, UINT8 Out[11]) {
    char Base[9];
    char Ext[4];
    int bi = 0;
    int ei = 0;
    int seenDot = 0;
    int i;
    int N;

    for (i = 0; i < 11; i++) {
        Out[i] = ' ';
    }
    Base[0] = 0;
    Ext[0] = 0;
    for (i = 0; LongName[i]; i++) {
        char C = ToUpper(LongName[i]);
        if (C == '.') {
            seenDot = 1;
            continue;
        }
        if (!((C >= 'A' && C <= 'Z') || (C >= '0' && C <= '9') ||
              C == '_' || C == '-')) {
            C = '_';
        }
        if (!seenDot) {
            if (bi < 8) {
                Base[bi++] = C;
            }
        } else if (ei < 3) {
            Ext[ei++] = C;
        }
    }
    Base[bi] = 0;
    Ext[ei] = 0;
    if (bi == 0) {
        Base[0] = 'X';
        Base[1] = 0;
        bi = 1;
    }

    for (N = 1; N <= 999999; N++) {
        char Trial[13];
        char Num[8];
        int ni = 0;
        int t;
        int Dig = N;
        int Prefix;
        UINT8 E[32];
        int Max;
        int Hit = 0;

        do {
            Num[ni++] = (char)('0' + (Dig % 10));
            Dig /= 10;
        } while (Dig > 0);
        Prefix = 8 - (1 + ni);
        if (Prefix < 1) {
            Prefix = 1;
        }
        if (Prefix > bi) {
            Prefix = bi;
        }
        for (i = 0; i < 11; i++) {
            Out[i] = ' ';
        }
        for (i = 0; i < Prefix; i++) {
            Out[i] = (UINT8)Base[i];
        }
        Out[Prefix] = '~';
        for (i = 0; i < ni; i++) {
            Out[Prefix + 1 + i] = (UINT8)Num[ni - 1 - i];
        }
        for (i = 0; i < ei && i < 3; i++) {
            Out[8 + i] = (UINT8)Ext[i];
        }

        ShortNameFromEntry(Out, Trial, sizeof(Trial));
        Max = DirMaxIndex(Dir);
        for (t = 0; t < Max; t++) {
            if (!DirReadEntry(Dir, (UINT32)t, E)) {
                break;
            }
            if (E[0] == 0x00) {
                break;
            }
            if (E[0] == 0xE5 || EntryIsLfn(E) || EntryIsVol(E)) {
                continue;
            }
            if (NameEqShort(E, Trial)) {
                Hit = 1;
                break;
            }
        }
        if (!Hit) {
            return 1;
        }
    }
    return 0;
}
