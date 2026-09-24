/*
 * FatFileRead.c — 偏移读（PR-S-fatfile-1）
 */
#include "Fat.h"
#include "FatPrivate.h"
#include "Block.h"

/* 从 First 簇跳过 Offset 字节，得到簇与簇内偏移 */
int SeekFileOffset(UINT32 First, UINTN Offset, UINT32 *OutCl, UINT32 *OutOff) {
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
