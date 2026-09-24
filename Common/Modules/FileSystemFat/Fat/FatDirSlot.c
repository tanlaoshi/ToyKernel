/*
 * FatDirSlot.c — 空闲槽、扩目录、建项（PR-S-fatdir-1）
 */
#include "Fat.h"
#include "FatPrivate.h"
#include "Block.h"
#include "LibWrite.h"

/*
 * 摘掉 SFN 前的 LFN 链。同扇区内的多项在内存里一并标 0xE5 再写回一次，
 * 避免 USB 写缓存下「读-改-写」把前一次 E5 盖回。
 * 不在此 BlockFlush：MSC 上 SYNCHRONIZE 失败曾导致刚写入被丢掉。
 */
int DeleteLfnPrefix(FAT_DIR_CTX Dir, int SfnIndex, UINT8 Cksum) {
    int i;
    UINT32 DirtyLba = 0xFFFFFFFFu;

    for (i = SfnIndex - 1; i >= 0; i--) {
        UINT8 E[32];
        UINT32 Lba = 0;
        UINT32 Off = 0;
        int Last;
        int k;

        if (!DirGetEntryPos(Dir, (UINT32)i, &Lba, &Off)) {
            return 0;
        }
        if (Lba != DirtyLba) {
            if (DirtyLba != 0xFFFFFFFFu) {
                if (!StoreSector(DirtyLba)) {
                    return 0;
                }
            }
            if (!LoadSector(Lba)) {
                return 0;
            }
            DirtyLba = Lba;
        }
        for (k = 0; k < 32; k++) {
            E[k] = gSector[Off + k];
        }
        if (!EntryIsLfn(E) || E[13] != Cksum) {
            break;
        }
        Last = (E[0] & 0x40) != 0;
        gSector[Off] = 0xE5;
        if (Last) {
            break;
        }
    }
    if (DirtyLba != 0xFFFFFFFFu) {
        if (!StoreSector(DirtyLba)) {
            return 0;
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
