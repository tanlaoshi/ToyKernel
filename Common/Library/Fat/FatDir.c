/*
 * FatDir.c — 目录枚举、mkdir/rmdir（PR-S-fatdir-1）
 * 扫描：FatDirScan.c；名字：FatDirName.c；建项：FatDirSlot.c；压测：FatDirStress.c
 */
#include "Fat.h"
#include "FatPrivate.h"
#include "Block.h"
#include "LibWrite.h"

FAT_DIRECTORY_ENTRY *gListOut;
int gListMax;
int gListCount;

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

int FatListEntries(const char *Path, FAT_DIRECTORY_ENTRY *Out, int Max, int *OutCount) {
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
    if (Attr & FAT_ATTR_RO) {
        return FAT_ERR_ROFS;
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
