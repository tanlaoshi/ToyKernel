/*
 * FatFile.c — 文件读/写/删/改名
 */
#include "Fat.h"
#include "FatPriv.h"
#include "Block.h"

static void (*gFatIoBreath)(void);

void FatSetIoBreath(void (*Fn)(void)) {
    gFatIoBreath = Fn;
}

static void FatIoBreath(void) {
    if (gFatIoBreath) {
        gFatIoBreath();
    }
}

int ReadFileClusters(UINT32 Cluster, UINT32 Size, void *Buffer, UINTN MaxSize,
                            UINTN *OutSize) {
    UINT8 *Dst = (UINT8 *)Buffer;
    UINTN Total = 0;
    UINT32 Remaining = Size;

    while (!ClusterEnd(Cluster) && Cluster >= 2 && Total < MaxSize) {
        if (!LoadCluster(Cluster)) {
            return 0;
        }
        {
            UINT32 Chunk = ClusterBytes();
            if (Chunk > Remaining) {
                Chunk = Remaining;
            }
            if (Total + Chunk > MaxSize) {
                Chunk = (UINT32)(MaxSize - Total);
            }
            for (UINT32 i = 0; i < Chunk; i++) {
                Dst[Total++] = gCluster[i];
            }
            Remaining -= Chunk;
        }
        Cluster = FatNext(Cluster);
    }
    if (OutSize) {
        *OutSize = Total;
    }
    return 1;
}
int FatReadFile(const char *Path, void *Buffer, UINTN MaxSize, UINTN *OutSize) {
    FAT_DIR_CTX Parent;
    char Leaf[FAT_NAME_MAX + 1];
    UINT32 Cluster = 0;
    UINT32 Size = 0;
    UINT8 Attr = 0;

    if (!Path || !Path[0] || !Buffer) {
        return FAT_ERR_INVAL;
    }
    if (!ResolvePathParentLeaf(Path, &Parent, Leaf)) {
        return FAT_ERR_NOENT;
    }
    if (CompIsDotOrDotDot(Leaf)) {
        return FAT_ERR_ISDIR;
    }
    if (!LookupInDir(Parent, Leaf, &Cluster, &Size, &Attr)) {
        return FAT_ERR_NOENT;
    }
    if (Attr & FAT_ATTR_DIR) {
        return FAT_ERR_ISDIR;
    }
    if (!ReadFileClusters(Cluster, Size, Buffer, MaxSize, OutSize)) {
        return FAT_ERR_IO;
    }
    return FAT_OK;
}

/* 从 First 簇跳过 Offset 字节，得到簇与簇内偏移 */
static int SeekFileOffset(UINT32 First, UINTN Offset, UINT32 *OutCl, UINT32 *OutOff) {
    UINT32 Cb = ClusterBytes();
    UINT32 Cl = First;
    UINTN Skip = Offset;

    if (First < 2) {
        return 0;
    }
    while (Skip >= Cb) {
        Cl = FatNext(Cl);
        if (Cl == 0xFFFFFFFFu || ClusterEnd(Cl) || Cl < 2) {
            return 0;
        }
        Skip -= Cb;
    }
    *OutCl = Cl;
    *OutOff = (UINT32)Skip;
    return 1;
}

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

int FatReadFileAt(const char *Path, UINTN Offset, void *Buffer, UINTN Len, UINTN *OutN) {
    FAT_DIR_CTX Parent;
    char Leaf[FAT_NAME_MAX + 1];
    UINT32 Cluster = 0;
    UINT32 Size = 0;
    UINT8 Attr = 0;
    UINT8 *Dst = (UINT8 *)Buffer;
    UINTN Got = 0;
    UINT32 Cl;
    UINT32 OffInCl;
    UINT32 Cb;

    if (OutN) {
        *OutN = 0;
    }
    if (!Path || !Path[0] || !Buffer) {
        return FAT_ERR_INVAL;
    }
    if (Len == 0) {
        return FAT_OK;
    }
    if (!ResolvePathParentLeaf(Path, &Parent, Leaf)) {
        return FAT_ERR_NOENT;
    }
    if (CompIsDotOrDotDot(Leaf)) {
        return FAT_ERR_ISDIR;
    }
    if (!LookupInDir(Parent, Leaf, &Cluster, &Size, &Attr)) {
        return FAT_ERR_NOENT;
    }
    if (Attr & FAT_ATTR_DIR) {
        return FAT_ERR_ISDIR;
    }
    if (Offset >= Size) {
        return FAT_OK;
    }
    if (Offset + Len > Size) {
        Len = Size - Offset;
    }
    if (Cluster < 2 || Len == 0) {
        return FAT_OK;
    }
    if (!SeekFileOffset(Cluster, Offset, &Cl, &OffInCl)) {
        return FAT_ERR_IO;
    }
    Cb = ClusterBytes();
    while (Got < Len) {
        UINT32 Chunk;
        UINT32 i;

        if (!LoadCluster(Cl)) {
            return FAT_ERR_IO;
        }
        Chunk = Cb - OffInCl;
        if (Chunk > Len - Got) {
            Chunk = (UINT32)(Len - Got);
        }
        for (i = 0; i < Chunk; i++) {
            Dst[Got + i] = gCluster[OffInCl + i];
        }
        Got += Chunk;
        OffInCl = 0;
        if (Got >= Len) {
            break;
        }
        Cl = FatNext(Cl);
        if (Cl == 0xFFFFFFFFu || ClusterEnd(Cl) || Cl < 2) {
            break;
        }
    }
    if (OutN) {
        *OutN = Got;
    }
    return FAT_OK;
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

int FatWriteFile(const char *Path, const void *Buffer, UINTN Size) {
    FAT_DIR_CTX Parent;
    char Leaf[FAT_NAME_MAX + 1];
    UINT32 FirstCluster = 0;
    UINT32 PrevCluster = 0;
    UINT32 NeedClusters;
    UINT32 Cb;
    UINT32 Written = 0;
    const UINT8 *Src = (const UINT8 *)Buffer;
    UINT32 i;
    int Rc;
    int Existing = 0;
    int Index = 0;
    UINT32 OldCluster = 0;
    UINT8 OldAttr = 0;

    if (!Path || (!Buffer && Size > 0)) {
        return FAT_ERR_INVAL;
    }
    if (Size > FAT_WRITE_MAX) {
        return FAT_ERR_FILE_TOO_BIG;
    }
    if (!ResolvePathParentLeaf(Path, &Parent, Leaf)) {
        return FAT_ERR_NOENT;
    }
    if (CompIsDotOrDotDot(Leaf)) {
        return FAT_ERR_INVAL;
    }
    if (FindDirIndex(Parent, Leaf, 1, &Index, &Existing, &OldCluster, &OldAttr) && Existing) {
        if (OldAttr & FAT_ATTR_DIR) {
            return FAT_ERR_ISDIR;
        }
        if (OldAttr & FAT_ATTR_RO) {
            return FAT_ERR_ROFS;
        }
    }

    Cb = ClusterBytes();
    /*
     * Size==0：仍分配 1 簇并写 1 字节 0，目录项 Size=1。
     * 纯 cluster=0 空文件在 QEMU vvfat 上常不创建宿主文件，重开目录即消失。
     */
    if (Size == 0) {
        static const UINT8 Pad[1] = { 0 };
        Src = Pad;
        Size = 1;
    }
    NeedClusters = (UINT32)((Size + Cb - 1) / Cb);

    /*
     * 已有文件：优先在旧簇链上就地覆写（可追加簇）。
     * QEMU vvfat：换新链再 FatFreeChain(旧簇) 会把宿主同名文件删掉，
     * 表现为 THEME.CFG「saved」后宿主文件消失、verify mode missing。
     */
    if (Existing && OldCluster >= 2) {
        UINT32 Cl = OldCluster;
        UINT8 E[32];
        UINT32 OldSize = 0;
        int DidExtend = 0;

        if (DirReadEntry(Parent, (UINT32)Index, E)) {
            OldSize = Read32(E + 28);
        }

        Written = 0;
        PrevCluster = 0;
        for (i = 0; i < NeedClusters; i++) {
            UINT32 Chunk;
            UINT32 Next;

            if (i == 0) {
                Cl = OldCluster;
            } else {
                Next = FatNext(PrevCluster);
                if (Next == 0xFFFFFFFFu) {
                    return FAT_ERR_IO;
                }
                if (ClusterEnd(Next) || Next < 2) {
                    UINT32 Neu = FatAllocCluster();
                    if (Neu < 2) {
                        return FAT_ERR_NOSPC;
                    }
                    if (!FatSet(PrevCluster, Neu)) {
                        FatFreeChain(Neu);
                        return FAT_ERR_IO;
                    }
                    Cl = Neu;
                    DidExtend = 1;
                } else {
                    Cl = Next;
                }
            }
            if (i == 0) {
                FirstCluster = Cl;
            }
            PrevCluster = Cl;

            Chunk = Cb;
            if (Written + Chunk > Size) {
                Chunk = (UINT32)(Size - Written);
            }
            for (UINT32 z = 0; z < Cb; z++) {
                gCluster[z] = 0;
            }
            for (UINT32 z = 0; z < Chunk; z++) {
                gCluster[z] = Src[Written + z];
            }
            if (!StoreCluster(Cl)) {
                return FAT_ERR_IO;
            }
            Written += Chunk;
            FatIoBreath();
        }
        /*
         * vvfat：同长覆写只写数据簇；勿无 FatSet 截断链、勿改目录项。
         * 改 Size / 扩链时才动元数据（缩短也不 FatFreeChain）。
         */
        if (DidExtend) {
            if (!FatSet(PrevCluster, EocValue())) {
                return FAT_ERR_IO;
            }
        }

        if (Size != OldSize || FirstCluster != OldCluster || DidExtend) {
            if (!DirReadEntry(Parent, (UINT32)Index, E)) {
                return FAT_ERR_IO;
            }
            E[11] = FAT_ATTR_ARCH;
            if (gFatType == 32) {
                Write16(E + 20, (UINT16)((FirstCluster >> 16) & 0xFFFF));
            }
            Write16(E + 26, (UINT16)(FirstCluster & 0xFFFF));
            Write32(E + 28, (UINT32)Size);
            if (!DirWriteEntry(Parent, (UINT32)Index, E)) {
                return FAT_ERR_IO;
            }
        }
        return FAT_OK;
    }

    /*
     * 新建，或已有但无簇（空项）：写新链。已有空项则改目录项，勿 Create（会重名失败）。
     */
    FirstCluster = 0;
    PrevCluster = 0;
    Written = 0;
    for (i = 0; i < NeedClusters; i++) {
        UINT32 Cl = FatAllocCluster();
        UINT32 Chunk;
        if (Cl < 2) {
            if (FirstCluster >= 2) {
                FatFreeChain(FirstCluster);
            }
            return FAT_ERR_NOSPC;
        }
        if (i == 0) {
            FirstCluster = Cl;
        } else if (!FatSet(PrevCluster, Cl)) {
            FatFreeChain(FirstCluster);
            return FAT_ERR_IO;
        }
        PrevCluster = Cl;

        Chunk = Cb;
        if (Written + Chunk > Size) {
            Chunk = (UINT32)(Size - Written);
        }
        for (UINT32 z = 0; z < Cb; z++) {
            gCluster[z] = 0;
        }
        for (UINT32 z = 0; z < Chunk; z++) {
            gCluster[z] = Src[Written + z];
        }
        if (!StoreCluster(Cl)) {
            FatFreeChain(FirstCluster);
            return FAT_ERR_IO;
        }
        Written += Chunk;
        FatIoBreath();
    }

    if (Existing) {
        UINT8 E[32];

        if (!DirReadEntry(Parent, (UINT32)Index, E)) {
            FatFreeChain(FirstCluster);
            return FAT_ERR_IO;
        }
        E[11] = FAT_ATTR_ARCH;
        if (gFatType == 32) {
            Write16(E + 20, (UINT16)((FirstCluster >> 16) & 0xFFFF));
        }
        Write16(E + 26, (UINT16)(FirstCluster & 0xFFFF));
        Write32(E + 28, (UINT32)Size);
        if (!DirWriteEntry(Parent, (UINT32)Index, E)) {
            FatFreeChain(FirstCluster);
            return FAT_ERR_IO;
        }
        return FAT_OK;
    }

    Rc = DirCreateEntry(Parent, Leaf, FAT_ATTR_ARCH, FirstCluster, (UINT32)Size);
    if (Rc != FAT_OK) {
        if (FirstCluster >= 2) {
            FatFreeChain(FirstCluster);
        }
        return Rc;
    }
    return FAT_OK;
}

static int FatDeleteFileOnce(const char *Path);

int FatDeleteFile(const char *Path) {
    int Round;
    int Any = 0;
    int Err;

    /*
     * 同名多目录项：最多摘几轮。勿再对 MSC 狂刷 SYNCHRONIZE（见 XhciMscFlush）。
     */
    for (Round = 0; Round < 4; Round++) {
        Err = FatDeleteFileOnce(Path);
        if (Err == FAT_ERR_NOENT) {
            return Any ? FAT_OK : FAT_ERR_NOENT;
        }
        if (Err != FAT_OK) {
            return Err;
        }
        Any = 1;
    }
    return FAT_OK;
}

static int FatDeleteFileOnce(const char *Path) {
    FAT_DIR_CTX Parent;
    char Leaf[FAT_NAME_MAX + 1];
    int Index = 0;
    int Existing = 0;
    UINT32 Cluster = 0;
    UINT8 Attr = 0;
    UINT8 E[32];
    UINT32 SfnLba = 0;
    UINT32 SfnOff = 0;
    UINT32 DirtyLba = 0xFFFFFFFFu;
    int i;
    UINT8 Cksum;

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
    if (Attr & FAT_ATTR_DIR) {
        FAT_DIR_CTX Sub;
        Sub.IsFat16Root = 0;
        Sub.Cluster = Cluster;
        if (Cluster < 2) {
            return FAT_ERR_INVAL;
        }
        if (!DirIsEmpty(Sub)) {
            return FAT_ERR_NOTEMPTY;
        }
    }
    if (!DirGetEntryPos(Parent, (UINT32)Index, &SfnLba, &SfnOff)) {
        return FAT_ERR_IO;
    }
    if (!LoadSector(SfnLba)) {
        return FAT_ERR_IO;
    }
    DirtyLba = SfnLba;
    for (i = 0; i < 32; i++) {
        E[i] = gSector[SfnOff + i];
    }
    /* 宿主 vvfat / 预制 ELF / USB 同步常带 RO/HIDDEN/SYSTEM；课堂允许删文件 */
    if ((Attr & (FAT_ATTR_RO | 0x02u | 0x04u)) && !(Attr & FAT_ATTR_DIR)) {
        E[11] = (UINT8)(Attr & (UINT8)~(FAT_ATTR_RO | 0x02u | 0x04u));
        for (i = 0; i < 32; i++) {
            gSector[SfnOff + i] = E[i];
        }
        Attr = E[11];
    } else if (Attr & FAT_ATTR_RO) {
        return FAT_ERR_ROFS;
    }

    Cksum = Fat83Checksum(E);
    /*
     * 先看 LFN 是否全在 SFN 同扇区。是则内存里一次改完再写；
     * 否则 DeleteLfnPrefix 跨扇区摘（只 WRITE，不 Flush）。
     */
    {
        int SameSectorLfn = 1;
        int j;

        for (j = Index - 1; j >= 0; j--) {
            UINT32 Lba = 0;
            UINT32 Off = 0;
            UINT8 T[32];
            int k;

            if (!DirGetEntryPos(Parent, (UINT32)j, &Lba, &Off)) {
                SameSectorLfn = 0;
                break;
            }
            if (Lba != SfnLba) {
                SameSectorLfn = 0;
                break;
            }
            for (k = 0; k < 32; k++) {
                T[k] = gSector[Off + k];
            }
            if (!EntryIsLfn(T) || T[13] != Cksum) {
                break;
            }
            if (T[0] & 0x40) {
                break;
            }
        }

        if (SameSectorLfn) {
            for (j = Index - 1; j >= 0; j--) {
                UINT32 Lba = 0;
                UINT32 Off = 0;
                UINT8 T[32];
                int k;
                int Last;

                if (!DirGetEntryPos(Parent, (UINT32)j, &Lba, &Off) || Lba != SfnLba) {
                    break;
                }
                for (k = 0; k < 32; k++) {
                    T[k] = gSector[Off + k];
                }
                if (!EntryIsLfn(T) || T[13] != Cksum) {
                    break;
                }
                Last = (T[0] & 0x40) != 0;
                gSector[Off] = 0xE5;
                if (Last) {
                    break;
                }
            }
        } else {
            if (!StoreSector(SfnLba)) {
                return FAT_ERR_IO;
            }
            DirtyLba = 0xFFFFFFFFu;
            if (!DeleteLfnPrefix(Parent, Index, Cksum)) {
                return FAT_ERR_IO;
            }
            if (!LoadSector(SfnLba)) {
                return FAT_ERR_IO;
            }
            DirtyLba = SfnLba;
            for (i = 0; i < 32; i++) {
                E[i] = gSector[SfnOff + i];
            }
        }
    }

    /*
     * 不在此 FatFreeChain：QEMU fat:rw(vvfat) 释放簇后再 commit 易断言。
     * 只标 0xE5；真盘会有簇泄漏，课堂可接受。
     */
    (void)Cluster;
    gSector[SfnOff] = 0xE5;
    if (DirtyLba != 0xFFFFFFFFu) {
        if (!StoreSector(DirtyLba)) {
            return FAT_ERR_IO;
        }
    }
    return FAT_OK;
}

/* 仅摘掉目录项，不释放簇（供 Rename） */
int DirUnlinkKeepClusters(FAT_DIR_CTX Parent, const char *Leaf) {
    int Index = 0;
    int Existing = 0;
    UINT32 Cluster = 0;
    UINT8 Attr = 0;
    UINT8 E[32];

    if (!FindDirIndex(Parent, Leaf, 1, &Index, &Existing, &Cluster, &Attr) || !Existing) {
        return FAT_ERR_NOENT;
    }
    if (!DirReadEntry(Parent, (UINT32)Index, E)) {
        return FAT_ERR_IO;
    }
    DeleteLfnPrefix(Parent, Index, Fat83Checksum(E));
    E[0] = 0xE5;
    if (!DirWriteEntry(Parent, (UINT32)Index, E)) {
        return FAT_ERR_IO;
    }
    return FAT_OK;
}

int FatRename(const char *OldPath, const char *NewPath) {
    FAT_DIR_CTX OldParent;
    FAT_DIR_CTX NewParent;
    char OldLeaf[FAT_NAME_MAX + 1];
    char NewLeaf[FAT_NAME_MAX + 1];
    UINT32 Cluster = 0;
    UINT32 Size = 0;
    UINT8 Attr = 0;
    int Index = 0;
    int Existing = 0;
    int Rc;

    if (!OldPath || !OldPath[0] || !NewPath || !NewPath[0]) {
        return FAT_ERR_INVAL;
    }
    if (!ResolvePathParentLeaf(OldPath, &OldParent, OldLeaf)) {
        return FAT_ERR_NOENT;
    }
    if (CompIsDotOrDotDot(OldLeaf)) {
        return FAT_ERR_INVAL;
    }
    if (!LookupInDir(OldParent, OldLeaf, &Cluster, &Size, &Attr)) {
        return FAT_ERR_NOENT;
    }
    if (Attr & FAT_ATTR_RO) {
        return FAT_ERR_ROFS;
    }
    if (!ResolvePathParentLeaf(NewPath, &NewParent, NewLeaf)) {
        return FAT_ERR_NOENT;
    }
    if (CompIsDotOrDotDot(NewLeaf)) {
        return FAT_ERR_INVAL;
    }
    if (FindDirIndex(NewParent, NewLeaf, 1, &Index, &Existing, 0, 0) && Existing) {
        return FAT_ERR_EXIST;
    }

    Rc = DirCreateEntry(NewParent, NewLeaf, Attr, Cluster, Size);
    if (Rc != FAT_OK) {
        return Rc;
    }
    Rc = DirUnlinkKeepClusters(OldParent, OldLeaf);
    if (Rc != FAT_OK) {
        /* 尽力回滚新名（会误释放簇，尽量避免走到这里） */
        (void)DirUnlinkKeepClusters(NewParent, NewLeaf);
        return Rc;
    }
    return FAT_OK;
}

int FatFileStat(const char *Path, FAT_FILE_STAT *Out) {
    FAT_DIR_CTX Parent;
    char Leaf[FAT_NAME_MAX + 1];
    UINT32 Cluster = 0;
    UINT32 Size = 0;
    UINT8 Attr = 0;

    if (!Out) {
        return FAT_ERR_INVAL;
    }
    /* 空路径或根：卷根目录 */
    if (!Path || !Path[0] || ((Path[0] == '/' || Path[0] == '\\') && !Path[1])) {
        Out->Attr = FAT_ATTR_DIR;
        Out->Size = 0;
        Out->Cluster = (gFatType == 32) ? gRootCluster : 0;
        return FAT_OK;
    }
    if (!ResolvePathParentLeaf(Path, &Parent, Leaf)) {
        return FAT_ERR_NOENT;
    }
    if (CompIsDot(Leaf)) {
        Out->Attr = FAT_ATTR_DIR;
        Out->Size = 0;
        Out->Cluster = Parent.IsFat16Root ? 0 : Parent.Cluster;
        return FAT_OK;
    }
    if (CompIsDotDot(Leaf)) {
        return FAT_ERR_INVAL;
    }
    if (!LookupInDir(Parent, Leaf, &Cluster, &Size, &Attr)) {
        return FAT_ERR_NOENT;
    }
    Out->Attr = Attr;
    Out->Size = Size;
    Out->Cluster = Cluster;
    return FAT_OK;
}

int FatFileSync(void) {
    if (!BlockFlush()) {
        return FAT_ERR_IO;
    }
    return FAT_OK;
}
