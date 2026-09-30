/*
 * FatFileWrite.c — 整文件写编排（PR-F-fat-1）
 * 就地覆写：FatFileWriteExist.c；新链：FatFileWriteNew.c
 */
#include "Fat.h"
#include "FatPrivate.h"

int FatWriteStoreChunk(UINT32 Cl, const UINT8 *Src, UINT32 Off, UINT32 Chunk, UINT32 Cb) {
    UINT32 z;

    for (z = 0; z < Cb; z++) {
        gCluster[z] = 0;
    }
    for (z = 0; z < Chunk; z++) {
        gCluster[z] = Src[Off + z];
    }
    if (!StoreCluster(Cl)) {
        return FAT_ERR_IO;
    }
    FatIoBreath();
    return FAT_OK;
}

int FatWriteFile(const char *Path, const void *Buffer, UINTN Size) {
    FAT_DIR_CTX Parent;
    char Leaf[FAT_NAME_MAX + 1];
    UINT32 NeedClusters;
    UINT32 Cb;
    const UINT8 *Src = (const UINT8 *)Buffer;
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
        return FatWriteExist(Parent, Index, OldCluster, Src, Size, NeedClusters, Cb);
    }

    /*
     * 新建，或已有但无簇（空项）：写新链。已有空项则改目录项，勿 Create（会重名失败）。
     */
    return FatWriteNew(Parent, Leaf, Existing, Index, Src, Size, NeedClusters, Cb);
}
