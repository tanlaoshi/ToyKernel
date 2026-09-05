/*
 * FatDir.c — 目录扫描、枚举、建项、mkdir/rmdir
 */
#include "Fat.h"
#include "FatPriv.h"
#include "Block.h"
#include "Console.h"

FAT_DIR_ENT *gListOut;
int gListMax;
int gListCount;

void PrintEntryNamed(const char *Name, UINT8 Attr) {
    if (Attr & FAT_ATTR_DIR) {
        ConsoleWrite("[DIR] ");
    } else {
        ConsoleWrite("      ");
    }
    ConsoleWrite(Name);
    ConsoleWrite("\n");
}

int ScanDirBuffer(UINT8 *Buf, UINT32 Bytes, const char *Name,
                         UINT32 *OutCluster, UINT32 *OutSize, int ListOnly,
                         UINT8 *OutAttr, FAT_LFN_ACC *Acc) {
    UINT32 Entries = Bytes / 32;
    UINT32 i;

    for (i = 0; i < Entries; i++) {
        UINT8 *E = Buf + i * 32;
        if (E[0] == 0x00) {
            LfnAccClear(Acc);
            return ListOnly ? 1 : 0;
        }
        if (E[0] == 0xE5) {
            LfnAccClear(Acc);
            continue;
        }
        if (EntryIsLfn(E)) {
            LfnFeed(Acc, E);
            continue;
        }
        if (EntryIsVol(E)) {
            LfnAccClear(Acc);
            continue;
        }
        if (ListOnly) {
            char Disp[FAT_NAME_MAX + 1];
            EntryDisplayName(E, Acc, Disp, sizeof(Disp));
            if (ListOnly == 2) {
                if (gListOut && gListCount < gListMax) {
                    int k;
                    for (k = 0; Disp[k] && k < FAT_ENT_NAME_MAX - 1; k++) {
                        gListOut[gListCount].Name[k] = Disp[k];
                    }
                    gListOut[gListCount].Name[k] = 0;
                    gListOut[gListCount].Attr = E[11];
                    gListOut[gListCount].Size = Read32(E + 28);
                    gListCount++;
                }
            } else {
                PrintEntryNamed(Disp, E[11]);
            }
            LfnAccClear(Acc);
            continue;
        }
        if (EntryMatchesName(E, Acc, Name)) {
            *OutCluster = EntryCluster(E);
            *OutSize = Read32(E + 28);
            if (OutAttr) {
                *OutAttr = E[11];
            }
            LfnAccClear(Acc);
            return 1;
        }
        LfnAccClear(Acc);
    }
    return ListOnly ? 1 : 0;
}

int ForEachDir(FAT_DIR_CTX Dir, int ListOnly, const char *Name,
                      UINT32 *OutCluster, UINT32 *OutSize, UINT8 *OutAttr) {
    FAT_LFN_ACC Acc;

    LfnAccClear(&Acc);
    if (Dir.IsFat16Root) {
        UINT32 s;
        for (s = 0; s < gRootSectors; s++) {
            if (!BlockReadSectors(gRootLba + s, 1, gSector)) {
                return 0;
            }
            if (!ListOnly) {
                if (ScanDirBuffer(gSector, SECTOR, Name, OutCluster, OutSize, 0, OutAttr, &Acc)) {
                    return 1;
                }
            } else if (!ScanDirBuffer(gSector, SECTOR, 0, 0, 0, ListOnly, 0, &Acc)) {
                return 0;
            }
        }
        return ListOnly;
    }

    {
        UINT32 Cluster = Dir.Cluster;
        while (!ClusterEnd(Cluster) && Cluster >= 2) {
            if (!LoadCluster(Cluster)) {
                return 0;
            }
            {
                UINT32 Bytes = ClusterBytes();
                if (!ListOnly) {
                    if (ScanDirBuffer(gCluster, Bytes, Name, OutCluster, OutSize, 0, OutAttr, &Acc)) {
                        return 1;
                    }
                } else if (!ScanDirBuffer(gCluster, Bytes, 0, 0, 0, ListOnly, 0, &Acc)) {
                    return 0;
                }
            }
            Cluster = FatNext(Cluster);
        }
    }
    return ListOnly;
}
int DirBufferHasEntries(UINT8 *Buf, UINT32 Bytes) {
    UINT32 Entries = Bytes / 32;
    UINT32 i;
    for (i = 0; i < Entries; i++) {
        UINT8 *E = Buf + i * 32;
        if (E[0] == 0x00) {
            return 0;
        }
        if (E[0] == 0xE5 || EntryIsLfn(E) || EntryIsVol(E)) {
            continue;
        }
        if (E[0] == '.' && (E[1] == ' ' || (E[1] == '.' && E[2] == ' '))) {
            continue;
        }
        return 1;
    }
    return 0;
}

int DirIsEmpty(FAT_DIR_CTX Dir) {
    if (Dir.IsFat16Root) {
        UINT32 s;
        for (s = 0; s < gRootSectors; s++) {
            if (!BlockReadSectors(gRootLba + s, 1, gSector)) {
                return 0;
            }
            if (DirBufferHasEntries(gSector, SECTOR)) {
                return 0;
            }
        }
        return 1;
    }

    {
        UINT32 Cluster = Dir.Cluster;
        while (!ClusterEnd(Cluster) && Cluster >= 2) {
            if (!LoadCluster(Cluster)) {
                return 0;
            }
            if (DirBufferHasEntries(gCluster, ClusterBytes())) {
                return 0;
            }
            Cluster = FatNext(Cluster);
        }
    }
    return 1;
}

int DirGetEntryPos(FAT_DIR_CTX Dir, UINT32 Index, UINT32 *Lba, UINT32 *Off) {
    UINT32 ByteOff = Index * 32;

    if (Dir.IsFat16Root) {
        if (ByteOff >= gRootSectors * SECTOR) {
            return 0;
        }
        *Lba = gRootLba + ByteOff / SECTOR;
        *Off = ByteOff % SECTOR;
        return 1;
    }
    {
        UINT32 Cluster = Dir.Cluster;
        UINT32 Cb = ClusterBytes();
        while (!ClusterEnd(Cluster) && Cluster >= 2) {
            if (ByteOff < Cb) {
                *Lba = gDataStart + (Cluster - 2) * gSectorsPerCluster + ByteOff / SECTOR;
                *Off = ByteOff % SECTOR;
                return 1;
            }
            ByteOff -= Cb;
            Cluster = FatNext(Cluster);
        }
    }
    return 0;
}

int DirReadEntry(FAT_DIR_CTX Dir, UINT32 Index, UINT8 Out[32]) {
    UINT32 Lba;
    UINT32 Off;
    int i;

    if (!DirGetEntryPos(Dir, Index, &Lba, &Off)) {
        return 0;
    }
    if (!LoadSector(Lba)) {
        return 0;
    }
    for (i = 0; i < 32; i++) {
        Out[i] = gSector[Off + i];
    }
    return 1;
}

int DirWriteEntry(FAT_DIR_CTX Dir, UINT32 Index, const UINT8 Ent[32]) {
    UINT32 Lba;
    UINT32 Off;
    int i;

    if (!DirGetEntryPos(Dir, Index, &Lba, &Off)) {
        return 0;
    }
    if (!LoadSector(Lba)) {
        return 0;
    }
    for (i = 0; i < 32; i++) {
        gSector[Off + i] = Ent[i];
    }
    return StoreSector(Lba);
}

int DirMaxIndex(FAT_DIR_CTX Dir) {
    if (Dir.IsFat16Root) {
        return (int)((gRootSectors * SECTOR) / 32);
    }
    {
        UINT32 Cluster = Dir.Cluster;
        UINT32 Total = 0;
        UINT32 Cb = ClusterBytes();
        while (!ClusterEnd(Cluster) && Cluster >= 2) {
            Total += Cb / 32;
            Cluster = FatNext(Cluster);
        }
        return (int)Total;
    }
}

int NameIsDot(const UINT8 *E) {
    return E[0] == '.' && (E[1] == ' ' || (E[1] == '.' && E[2] == ' '));
}
/*
 * 查找 Name 对应 SFN 下标，或 Need 个连续空闲槽起点。
 * 找到已存在时 *FoundExisting=1 且 *OutIndex 为 SFN；空闲时为第一槽。
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

    *FoundExisting = 0;
    *OldCluster = 0;
    if (OldAttr) {
        *OldAttr = 0;
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
            FreeRun = Max - i;
            if (FreeRun >= Need) {
                *OutIndex = FreeStart;
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
            if (FreeRun >= Need) {
                *OutIndex = FreeStart;
                return 1;
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

int DeleteLfnPrefix(FAT_DIR_CTX Dir, int SfnIndex, UINT8 Cksum) {
    int i;
    for (i = SfnIndex - 1; i >= 0; i--) {
        UINT8 E[32];
        int Last;
        if (!DirReadEntry(Dir, (UINT32)i, E)) {
            return 0;
        }
        if (!EntryIsLfn(E) || E[13] != Cksum) {
            break;
        }
        Last = (E[0] & 0x40) != 0;
        E[0] = 0xE5;
        if (!DirWriteEntry(Dir, (UINT32)i, E)) {
            return 0;
        }
        if (Last) {
            break;
        }
    }
    return 1;
}

int FindFreeRun(FAT_DIR_CTX Dir, int Need, int *OutIndex) {
    int Max = DirMaxIndex(Dir);
    int FreeRun = 0;
    int FreeStart = 0;
    int t;

    for (t = 0; t < Max; t++) {
        UINT8 Tmp[32];
        if (!DirReadEntry(Dir, (UINT32)t, Tmp)) {
            return 0;
        }
        if (Tmp[0] == 0x00) {
            if (FreeRun == 0) {
                FreeStart = t;
            }
            if (Max - FreeStart >= Need) {
                *OutIndex = FreeStart;
                return 1;
            }
            return 0;
        }
        if (Tmp[0] == 0xE5) {
            if (FreeRun == 0) {
                FreeStart = t;
            }
            FreeRun++;
            if (FreeRun >= Need) {
                *OutIndex = FreeStart;
                return 1;
            }
        } else {
            FreeRun = 0;
        }
    }
    return 0;
}

/* FAT32/子目录：目录簇满时追加新簇；FAT16 根不可扩展 */
int DirGrow(FAT_DIR_CTX Dir) {
    UINT32 Tail;
    UINT32 Next;
    UINT32 New;
    UINT32 z;
    UINT32 Cb;

    if (Dir.IsFat16Root) {
        return 0;
    }
    Tail = Dir.Cluster;
    if (Tail < 2) {
        return 0;
    }
    for (;;) {
        Next = FatNext(Tail);
        if (Next == 0xFFFFFFFFu) {
            return 0;
        }
        if (ClusterEnd(Next)) {
            break;
        }
        if (Next < 2) {
            return 0;
        }
        Tail = Next;
    }
    New = FatAllocCluster();
    if (New < 2) {
        return 0;
    }
    Cb = ClusterBytes();
    for (z = 0; z < Cb; z++) {
        gCluster[z] = 0;
    }
    if (!StoreCluster(New)) {
        FatSet(New, 0);
        return 0;
    }
    if (!FatSet(Tail, New)) {
        FatSet(New, 0);
        return 0;
    }
    return 1;
}

int DirFindSlots(FAT_DIR_CTX Dir, int Need, int *OutIndex) {
    int Grow;

    for (Grow = 0; Grow < 8; Grow++) {
        if (FindFreeRun(Dir, Need, OutIndex)) {
            return 1;
        }
        if (!DirGrow(Dir)) {
            return 0;
        }
    }
    return 0;
}

void FillSfnEntry(UINT8 *E, const UINT8 Name83[11], UINT8 Attr, UINT32 Cluster,
                         UINT32 Size) {
    UINT32 i;

    for (i = 0; i < 32; i++) {
        E[i] = 0;
    }
    for (i = 0; i < 11; i++) {
        E[i] = Name83[i];
    }
    E[11] = Attr;
    if (gFatType == 32) {
        Write16(E + 20, (UINT16)((Cluster >> 16) & 0xFFFF));
    }
    Write16(E + 26, (UINT16)(Cluster & 0xFFFF));
    Write32(E + 28, Size);
}

UINT32 ParentClusterForDotDot(FAT_DIR_CTX Parent) {
    if (Parent.IsFat16Root) {
        return 0;
    }
    if (gFatType == 32 && Parent.Cluster == gRootCluster) {
        return 0;
    }
    return Parent.Cluster;
}

/* 在 Parent 中写入 LFN+SFN；Existing 时覆盖。成功 FAT_OK */
int DirCreateEntry(FAT_DIR_CTX Parent, const char *Leaf, UINT8 Attr,
                          UINT32 Cluster, UINT32 Size) {
    UINT8 Name83[11];
    int LfnCount;
    int Need;
    int Index = 0;
    int Existing = 0;
    UINT32 OldCluster = 0;
    UINT8 OldAttr = 0;
    UINT8 E[32];
    UINT8 Cksum;
    int SfnIndex;
    int Ord;

    if (!Leaf || !Leaf[0] || CompIsDotOrDotDot(Leaf)) {
        return FAT_ERR_INVAL;
    }
    /*
     * 始终写 LFN：纯 8.3 短名会 ToUpper，QEMU vvfat 宿主侧又常显示成小写，
     * 重启后列表大小写与用户输入不一致。LFN 保留原始大小写。
     */
    if (!PathTo83(Leaf, Name83)) {
        if (!Make83Alias(Parent, Leaf, Name83)) {
            return FAT_ERR_NOSPC;
        }
    }
    LfnCount = LfnEntryCountForName(Leaf);
    if (LfnCount <= 0 || LfnCount > 20) {
        return FAT_ERR_NAMETOOLONG;
    }
    Need = LfnCount + 1;

    if (!FindDirIndex(Parent, Leaf, Need, &Index, &Existing, &OldCluster, &OldAttr)) {
        if (!DirFindSlots(Parent, Need, &Index)) {
            return FAT_ERR_NOSPC;
        }
        Existing = 0;
    }

    if (Existing) {
        UINT8 Old[32];
        UINT32 DeferredFree;

        if (!DirReadEntry(Parent, (UINT32)Index, Old)) {
            return FAT_ERR_IO;
        }
        /*
         * PR-FS3：先腾槽位再写新项，最后才释放旧簇。
         * 若在释放后 FindSlots/写项失败，会丢目录项。
         */
        DeferredFree = OldCluster;
        DeleteLfnPrefix(Parent, Index, Fat83Checksum(Old));
        Old[0] = 0xE5;
        if (!DirWriteEntry(Parent, (UINT32)Index, Old)) {
            return FAT_ERR_IO;
        }
        if (!DirFindSlots(Parent, Need, &Index)) {
            return FAT_ERR_NOSPC;
        }
        SfnIndex = Index + LfnCount;
        Cksum = Fat83Checksum(Name83);
        for (Ord = LfnCount; Ord >= 1; Ord--) {
            FillLfnEntry(E, Ord, Ord == LfnCount, Cksum, Leaf);
            if (!DirWriteEntry(Parent, (UINT32)(SfnIndex - Ord), E)) {
                return FAT_ERR_IO;
            }
        }
        FillSfnEntry(E, Name83, Attr, Cluster, Size);
        if (!DirWriteEntry(Parent, (UINT32)SfnIndex, E)) {
            return FAT_ERR_IO;
        }
        if (DeferredFree >= 2 && DeferredFree != Cluster) {
            FatFreeChain(DeferredFree);
        }
        return FAT_OK;
    }

    SfnIndex = Index + LfnCount;
    Cksum = Fat83Checksum(Name83);
    for (Ord = LfnCount; Ord >= 1; Ord--) {
        FillLfnEntry(E, Ord, Ord == LfnCount, Cksum, Leaf);
        if (!DirWriteEntry(Parent, (UINT32)(SfnIndex - Ord), E)) {
            return FAT_ERR_IO;
        }
    }
    FillSfnEntry(E, Name83, Attr, Cluster, Size);
    if (!DirWriteEntry(Parent, (UINT32)SfnIndex, E)) {
        return FAT_ERR_IO;
    }
    return FAT_OK;
}
int FatListRoot(void) {
    return FatListDir(0);
}

int FatListDir(const char *Path) {
    FAT_DIR_CTX Dir;

    if (!ResolvePathAsDir(Path ? Path : "", &Dir)) {
        return FAT_ERR_NOENT;
    }
    if (!ForEachDir(Dir, 1, 0, 0, 0, 0)) {
        return FAT_ERR_IO;
    }
    return FAT_OK;
}

int FatListEntries(const char *Path, FAT_DIR_ENT *Out, int Max, int *OutCount) {
    FAT_DIR_CTX Dir;

    if (!Out || !OutCount || Max <= 0) {
        return FAT_ERR_INVAL;
    }
    gListOut = Out;
    gListMax = Max;
    gListCount = 0;
    if (!ResolvePathAsDir(Path ? Path : "", &Dir)) {
        gListOut = 0;
        return FAT_ERR_NOENT;
    }
    if (!ForEachDir(Dir, 2, 0, 0, 0, 0)) {
        gListOut = 0;
        return FAT_ERR_IO;
    }
    *OutCount = gListCount;
    gListOut = 0;
    return FAT_OK;
}
int FatMkdir(const char *Path) {
    FAT_DIR_CTX Parent;
    char Leaf[FAT_NAME_MAX + 1];
    UINT32 NewCl;
    UINT32 DotDotCl;
    UINT8 NameDot[11];
    UINT8 NameDotDot[11];
    UINT8 E[32];
    UINT32 z;
    UINT32 Cb;
    int Index = 0;
    int Existing = 0;
    UINT32 OldCluster = 0;
    int Rc;

    if (!Path || !Path[0]) {
        return FAT_ERR_INVAL;
    }
    if (!ResolvePathParentLeaf(Path, &Parent, Leaf)) {
        return FAT_ERR_NOENT;
    }
    if (CompIsDotOrDotDot(Leaf)) {
        return FAT_ERR_INVAL;
    }
    if (FindDirIndex(Parent, Leaf, 1, &Index, &Existing, &OldCluster, 0) && Existing) {
        return FAT_ERR_EXIST;
    }

    NewCl = FatAllocCluster();
    if (NewCl < 2) {
        return FAT_ERR_NOSPC;
    }
    Cb = ClusterBytes();
    for (z = 0; z < Cb; z++) {
        gCluster[z] = 0;
    }
    for (z = 0; z < 11; z++) {
        NameDot[z] = ' ';
        NameDotDot[z] = ' ';
    }
    NameDot[0] = '.';
    NameDotDot[0] = '.';
    NameDotDot[1] = '.';
    DotDotCl = ParentClusterForDotDot(Parent);
    FillSfnEntry(E, NameDot, FAT_ATTR_DIR, NewCl, 0);
    for (z = 0; z < 32; z++) {
        gCluster[z] = E[z];
    }
    FillSfnEntry(E, NameDotDot, FAT_ATTR_DIR, DotDotCl, 0);
    for (z = 0; z < 32; z++) {
        gCluster[32 + z] = E[z];
    }
    if (!StoreCluster(NewCl)) {
        FatSet(NewCl, 0);
        return FAT_ERR_IO;
    }

    Rc = DirCreateEntry(Parent, Leaf, FAT_ATTR_DIR, NewCl, 0);
    if (Rc != FAT_OK) {
        FatFreeChain(NewCl);
        return Rc;
    }
    return FAT_OK;
}

int FatRmdir(const char *Path) {
    FAT_DIR_CTX Parent;
    char Leaf[FAT_NAME_MAX + 1];
    int Index = 0;
    int Existing = 0;
    UINT32 Cluster = 0;
    UINT8 Attr = 0;
    UINT8 E[32];
    FAT_DIR_CTX Sub;

    if (!Path || !Path[0]) {
        return FAT_ERR_INVAL;
    }
    if (!ResolvePathParentLeaf(Path, &Parent, Leaf)) {
        return FAT_ERR_NOENT;
    }
    if (CompIsDotOrDotDot(Leaf)) {
        return FAT_ERR_INVAL;
    }
    if (!FindDirIndex(Parent, Leaf, 1, &Index, &Existing, &Cluster, &Attr) || !Existing) {
        return FAT_ERR_NOENT;
    }
    if (!(Attr & FAT_ATTR_DIR)) {
        return FAT_ERR_NOTDIR;
    }
    if (Cluster < 2) {
        return FAT_ERR_INVAL;
    }
    Sub.IsFat16Root = 0;
    Sub.Cluster = Cluster;
    if (!DirIsEmpty(Sub)) {
        return FAT_ERR_NOTEMPTY;
    }
    if (!DirReadEntry(Parent, (UINT32)Index, E)) {
        return FAT_ERR_IO;
    }
    DeleteLfnPrefix(Parent, Index, Fat83Checksum(E));
    if (!FatFreeChain(Cluster)) {
        return FAT_ERR_IO;
    }
    E[0] = 0xE5;
    if (!DirWriteEntry(Parent, (UINT32)Index, E)) {
        return FAT_ERR_IO;
    }
    return FAT_OK;
}
