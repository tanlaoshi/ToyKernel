/*
 * FatFile.c — 整文件读、同步、IO 喘息（PR-S-fatfile-1）
 * 偏移读：FatFileRead.c；写：FatFileWrite.c；删改名：FatFileDelete.c
 */
#include "Fat.h"
#include "FatPrivate.h"
#include "Block.h"

static void (*gFatIoBreath)(void);

void FatSetIoBreath(void (*Fn)(void)) {
    gFatIoBreath = Fn;
}

void FatIoBreath(void) {
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
