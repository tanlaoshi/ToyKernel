/*
 * FatFile.c — 文件读/写/删/改名
 */
#include "Fat.h"
#include "FatPriv.h"
#include "Block.h"

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
        return FAT_ERR_FBIG;
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
     * 先写全新簇链，目录项仍指向旧文件；失败只回滚新链，旧项保留（PR-FS3）。
     */
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
    }

    if (Existing) {
        UINT8 E[32];

        /* 同名覆盖：就地改 SFN 的簇/大小，再释放旧链 */
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
        if (OldCluster >= 2 && OldCluster != FirstCluster) {
            FatFreeChain(OldCluster);
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

int FatDeleteFile(const char *Path) {
    FAT_DIR_CTX Parent;
    char Leaf[FAT_NAME_MAX + 1];
    int Index = 0;
    int Existing = 0;
    UINT32 Cluster = 0;
    UINT8 Attr = 0;
    UINT8 E[32];

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
    if (Attr & FAT_ATTR_RO) {
        return FAT_ERR_ROFS;
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
    if (!DirReadEntry(Parent, (UINT32)Index, E)) {
        return FAT_ERR_IO;
    }
    DeleteLfnPrefix(Parent, Index, Fat83Checksum(E));
    if (Cluster >= 2 && !FatFreeChain(Cluster)) {
        return FAT_ERR_IO;
    }
    E[0] = 0xE5;
    if (!DirWriteEntry(Parent, (UINT32)Index, E)) {
        return FAT_ERR_IO;
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
