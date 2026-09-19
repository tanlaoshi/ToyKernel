/*
 * FatFileWriteAt.c — 偏移写（PR-S-fatfile-1）
 */
#include "Fat.h"
#include "FatPriv.h"
#include "Block.h"

/* 保证从 First 起至少有 NeedClusters 个簇；可扩展。成功更新 *InOutFirst */
static int EnsureClusterCount(UINT32 *InOutFirst, UINT32 NeedClusters) {
    UINT32 First = *InOutFirst;
    UINT32 Prev = 0;
    UINT32 Cl;
    UINT32 i;

    if (NeedClusters == 0) {
        return 1;
    }
    if (First < 2) {
        First = 0;
        Prev = 0;
        for (i = 0; i < NeedClusters; i++) {
            Cl = FatAllocCluster();
            if (Cl < 2) {
                if (First >= 2) {
                    FatFreeChain(First);
                }
                return 0;
            }
            if (i == 0) {
                First = Cl;
            } else if (!FatSet(Prev, Cl)) {
                FatFreeChain(First);
                return 0;
            }
            Prev = Cl;
            {
                UINT32 z;
                UINT32 Cb = ClusterBytes();
                for (z = 0; z < Cb; z++) {
                    gCluster[z] = 0;
                }
                if (!StoreCluster(Cl)) {
                    FatFreeChain(First);
                    return 0;
                }
            }
        }
        if (!FatSet(Prev, EocValue())) {
            FatFreeChain(First);
            return 0;
        }
        *InOutFirst = First;
        return 1;
    }

    Cl = First;
    Prev = 0;
    for (i = 0; i < NeedClusters; i++) {
        if (i == 0) {
            Cl = First;
        } else {
            UINT32 Next = FatNext(Prev);
            if (Next == 0xFFFFFFFFu) {
                return 0;
            }
            if (ClusterEnd(Next) || Next < 2) {
                UINT32 Neu = FatAllocCluster();
                UINT32 z;
                UINT32 Cb;
                if (Neu < 2) {
                    return 0;
                }
                if (!FatSet(Prev, Neu)) {
                    FatFreeChain(Neu);
                    return 0;
                }
                Cb = ClusterBytes();
                for (z = 0; z < Cb; z++) {
                    gCluster[z] = 0;
                }
                if (!StoreCluster(Neu)) {
                    return 0;
                }
                Cl = Neu;
                if (!FatSet(Cl, EocValue())) {
                    return 0;
                }
            } else {
                Cl = Next;
            }
        }
        Prev = Cl;
    }
    *InOutFirst = First;
    return 1;
}

int FatWriteFileAt(const char *Path, UINTN Offset, const void *Buffer, UINTN Len, UINTN *OutN) {
    FAT_DIR_CTX Parent;
    char Leaf[FAT_NAME_MAX + 1];
    UINT32 FirstCluster = 0;
    UINT32 OldSize = 0;
    UINT8 OldAttr = 0;
    int Existing = 0;
    int Index = 0;
    UINT32 FinalSize;
    UINT32 NeedClusters;
    UINT32 Cb;
    UINT32 Cl;
    UINT32 OffInCl;
    const UINT8 *Src = (const UINT8 *)Buffer;
    UINTN Done = 0;
    UINT8 E[32];

    if (OutN) {
        *OutN = 0;
    }
    if (!Path || (!Buffer && Len > 0)) {
        return FAT_ERR_INVAL;
    }
    if (Len == 0) {
        return FAT_OK;
    }
    if (Offset > FAT_WRITE_MAX || Len > FAT_WRITE_MAX || Offset + Len > FAT_WRITE_MAX) {
        return FAT_ERR_FILE_TOO_BIG;
    }
    if (!ResolvePathParentLeaf(Path, &Parent, Leaf)) {
        return FAT_ERR_NOENT;
    }
    if (CompIsDotOrDotDot(Leaf)) {
        return FAT_ERR_INVAL;
    }
    if (FindDirIndex(Parent, Leaf, 1, &Index, &Existing, &FirstCluster, &OldAttr) && Existing) {
        if (OldAttr & FAT_ATTR_DIR) {
            return FAT_ERR_ISDIR;
        }
        if (OldAttr & FAT_ATTR_RO) {
            return FAT_ERR_ROFS;
        }
        if (DirReadEntry(Parent, (UINT32)Index, E)) {
            OldSize = Read32(E + 28);
            FirstCluster = EntryCluster(E);
        } else {
            OldSize = 0;
        }
    } else {
        Existing = 0;
        FirstCluster = 0;
        OldSize = 0;
        /* USB：上块 Write 可能已建项但本趟 Find 读到旧目录 → 再刷再找，防重复建项 */
        (void)BlockFlush();
        if (FindDirIndex(Parent, Leaf, 1, &Index, &Existing, &FirstCluster, &OldAttr) &&
            Existing) {
            if (OldAttr & FAT_ATTR_DIR) {
                return FAT_ERR_ISDIR;
            }
            if (DirReadEntry(Parent, (UINT32)Index, E)) {
                OldSize = Read32(E + 28);
                FirstCluster = EntryCluster(E);
            }
        } else {
            Existing = 0;
            FirstCluster = 0;
            OldSize = 0;
        }
    }

    FinalSize = (UINT32)(Offset + Len);
    if (FinalSize < OldSize) {
        FinalSize = OldSize;
    }
    if (FinalSize > FAT_WRITE_MAX) {
        return FAT_ERR_FILE_TOO_BIG;
    }

    Cb = ClusterBytes();
    /*
     * 与 FatWriteFile 一致：Size==0 仍占 1 簇；此处至少写 Len>0，按 FinalSize 算簇数。
     */
    NeedClusters = (FinalSize + Cb - 1) / Cb;
    if (NeedClusters == 0) {
        NeedClusters = 1;
    }
    if (!EnsureClusterCount(&FirstCluster, NeedClusters)) {
        return FAT_ERR_NOSPC;
    }

    if (!SeekFileOffset(FirstCluster, Offset, &Cl, &OffInCl)) {
        return FAT_ERR_IO;
    }
    while (Done < Len) {
        UINT32 Chunk;
        UINT32 i;

        if (!LoadCluster(Cl)) {
            return FAT_ERR_IO;
        }
        Chunk = Cb - OffInCl;
        if (Chunk > Len - Done) {
            Chunk = (UINT32)(Len - Done);
        }
        for (i = 0; i < Chunk; i++) {
            gCluster[OffInCl + i] = Src[Done + i];
        }
        if (!StoreCluster(Cl)) {
            return FAT_ERR_IO;
        }
        FatIoBreath();
        Done += Chunk;
        OffInCl = 0;
        if (Done >= Len) {
            break;
        }
        Cl = FatNext(Cl);
        if (Cl == 0xFFFFFFFFu || ClusterEnd(Cl) || Cl < 2) {
            return FAT_ERR_IO;
        }
    }

    if (Existing) {
        if (!DirReadEntry(Parent, (UINT32)Index, E)) {
            return FAT_ERR_IO;
        }
        E[11] = FAT_ATTR_ARCH;
        if (gFatType == 32) {
            Write16(E + 20, (UINT16)((FirstCluster >> 16) & 0xFFFF));
        }
        Write16(E + 26, (UINT16)(FirstCluster & 0xFFFF));
        Write32(E + 28, FinalSize);
        if (!DirWriteEntry(Parent, (UINT32)Index, E)) {
            return FAT_ERR_IO;
        }
    } else {
        int Rc = DirCreateEntry(Parent, Leaf, FAT_ATTR_ARCH, FirstCluster, FinalSize);
        if (Rc != FAT_OK) {
            FatFreeChain(FirstCluster);
            return Rc;
        }
    }

    if (OutN) {
        *OutN = Done;
    }
    return FAT_OK;
}
