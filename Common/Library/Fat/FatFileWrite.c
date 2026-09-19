/*
 * FatFileWrite.c — 整文件写（PR-S-fatfile-1）
 */
#include "Fat.h"
#include "FatPrivate.h"
#include "Block.h"

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
