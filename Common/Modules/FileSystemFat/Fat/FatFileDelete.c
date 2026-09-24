/*
 * FatFileDelete.c — 删除与改名（PR-S-fatfile-1）
 */
#include "Fat.h"
#include "FatPrivate.h"
#include "Block.h"

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
