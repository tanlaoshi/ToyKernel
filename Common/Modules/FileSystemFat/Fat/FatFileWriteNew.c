/*
 * FatFileWriteNew.c — 分配新簇链并建/改目录项（PR-F-fat-1）
 */
#include "Fat.h"
#include "FatPrivate.h"

int FatWriteNew(FAT_DIR_CTX Parent, const char *Leaf, int Existing, int Index,
                const UINT8 *Src, UINTN Size, UINT32 NeedClusters, UINT32 Cb) {
    UINT32 FirstCluster = 0;
    UINT32 PrevCluster = 0;
    UINT32 Written = 0;
    UINT32 i;
    int Rc;

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
        Rc = FatWriteStoreChunk(Cl, Src, Written, Chunk, Cb);
        if (Rc != FAT_OK) {
            FatFreeChain(FirstCluster);
            return Rc;
        }
        Written += Chunk;
    }

    if (Existing) {
        UINT8 E[32];

        if (!DirReadEntry(Parent, (UINT32)Index, E)) {
            FatFreeChain(FirstCluster);
            return FAT_ERR_IO;
        }
        E[11] = FAT_ATTR_ARCH;
        if (gFatType == 32) {
            Write16(E + 20, (UINT16)((FirstCluster >> 16) & FAT_U16_MASK));
        }
        Write16(E + 26, (UINT16)(FirstCluster & FAT_U16_MASK));
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
