/*
 * FatFileWriteExist.c — 旧簇链就地覆写（PR-F-fat-1）
 */
#include "Fat.h"
#include "FatPrivate.h"

int FatWriteExist(FAT_DIR_CTX Parent, int Index, UINT32 OldCluster,
                  const UINT8 *Src, UINTN Size, UINT32 NeedClusters, UINT32 Cb) {
    UINT32 FirstCluster = 0;
    UINT32 PrevCluster = 0;
    UINT32 Written = 0;
    UINT32 i;
    UINT32 Cl = OldCluster;
    UINT8 E[32];
    UINT32 OldSize = 0;
    int DidExtend = 0;

    if (DirReadEntry(Parent, (UINT32)Index, E)) {
        OldSize = Read32(E + 28);
    }

    for (i = 0; i < NeedClusters; i++) {
        UINT32 Chunk;
        UINT32 Next;
        int Rc;

        if (i == 0) {
            Cl = OldCluster;
        } else {
            Next = FatNext(PrevCluster);
            if (Next == FAT_CLUSTER_INVALID) {
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
        Rc = FatWriteStoreChunk(Cl, Src, Written, Chunk, Cb);
        if (Rc != FAT_OK) {
            return Rc;
        }
        Written += Chunk;
    }
    /*
     * vvfat：同长覆写只写数据簇；勿用 FatSet 截断链、勿改目录项。
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
            Write16(E + 20, (UINT16)((FirstCluster >> 16) & FAT_U16_MASK));
        }
        Write16(E + 26, (UINT16)(FirstCluster & FAT_U16_MASK));
        Write32(E + 28, (UINT32)Size);
        if (!DirWriteEntry(Parent, (UINT32)Index, E)) {
            return FAT_ERR_IO;
        }
    }
    return FAT_OK;
}
