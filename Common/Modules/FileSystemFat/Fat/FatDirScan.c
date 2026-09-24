/*
 * FatDirScan.c — 目录缓冲扫描与项读写（PR-S-fatdir-1）
 */
#include "Fat.h"
#include "FatPrivate.h"
#include "Block.h"
#include "LibWrite.h"

void PrintEntryNamed(const char *Name, UINT8 Attr) {
    if (Attr & FAT_ATTR_DIR) {
        LibWrite("[DIR] ");
    } else {
        LibWrite("      ");
    }
    LibWrite(Name);
    LibWrite("\n");
}

int ScanDirBuffer(UINT8 *Buf, UINT32 Bytes, const char *Name,
                         UINT32 *OutCluster, UINT32 *OutSize, int ListOnly,
                         UINT8 *OutAttr, FAT_LFN_ACC *Acc) {
    UINT32 Entries = Bytes / 32;
    UINT32 i;

    for (i = 0; i < Entries; i++) {
        UINT8 *E = Buf + i * 32;
        if (E[0] == 0x00) {
            LfnAccClear(Acc);
            return ListOnly ? 1 : 0;
        }
        if (E[0] == 0xE5) {
            LfnAccClear(Acc);
            continue;
        }
        if (EntryIsLfn(E)) {
            LfnFeed(Acc, E);
            continue;
        }
        if (EntryIsVol(E)) {
            LfnAccClear(Acc);
            continue;
        }
        if (ListOnly) {
            char Disp[FAT_NAME_MAX + 1];
            EntryDisplayName(E, Acc, Disp, sizeof(Disp));
            if (ListOnly == 2) {
                if (gListOut && gListCount < gListMax) {
                    int k;
                    for (k = 0; Disp[k] && k < FAT_ENT_NAME_MAX - 1; k++) {
                        gListOut[gListCount].Name[k] = Disp[k];
                    }
                    gListOut[gListCount].Name[k] = 0;
                    gListOut[gListCount].Attr = E[11];
                    gListOut[gListCount].Size = Read32(E + 28);
                    gListCount++;
                }
            } else {
                PrintEntryNamed(Disp, E[11]);
            }
            LfnAccClear(Acc);
            continue;
        }
        if (EntryMatchesName(E, Acc, Name)) {
            *OutCluster = EntryCluster(E);
            *OutSize = Read32(E + 28);
            if (OutAttr) {
                *OutAttr = E[11];
            }
            LfnAccClear(Acc);
            return 1;
        }
        LfnAccClear(Acc);
    }
    return ListOnly ? 1 : 0;
}

int ForEachDir(FAT_DIR_CTX Dir, int ListOnly, const char *Name,
                      UINT32 *OutCluster, UINT32 *OutSize, UINT8 *OutAttr) {
    FAT_LFN_ACC Acc;

    LfnAccClear(&Acc);
    if (Dir.IsFat16Root) {
        UINT32 s;
        for (s = 0; s < gRootSectors; s++) {
            if (!BlockReadSectors(gRootLba + s, 1, gSector)) {
                return 0;
            }
            if (!ListOnly) {
                if (ScanDirBuffer(gSector, SECTOR, Name, OutCluster, OutSize, 0, OutAttr, &Acc)) {
                    return 1;
                }
            } else if (!ScanDirBuffer(gSector, SECTOR, 0, 0, 0, ListOnly, 0, &Acc)) {
                return 0;
            }
        }
        return ListOnly;
    }

    {
        UINT32 Cluster = Dir.Cluster;
        while (!ClusterEnd(Cluster) && Cluster >= 2) {
            if (!LoadCluster(Cluster)) {
                return 0;
            }
            {
                UINT32 Bytes = ClusterBytes();
                if (!ListOnly) {
                    if (ScanDirBuffer(gCluster, Bytes, Name, OutCluster, OutSize, 0, OutAttr, &Acc)) {
                        return 1;
                    }
                } else if (!ScanDirBuffer(gCluster, Bytes, 0, 0, 0, ListOnly, 0, &Acc)) {
                    return 0;
                }
            }
            Cluster = FatNext(Cluster);
        }
    }
    return ListOnly;
}
int DirBufferHasEntries(UINT8 *Buf, UINT32 Bytes) {
    UINT32 Entries = Bytes / 32;
    UINT32 i;
    for (i = 0; i < Entries; i++) {
        UINT8 *E = Buf + i * 32;
        if (E[0] == 0x00) {
            return 0;
        }
        if (E[0] == 0xE5 || EntryIsLfn(E) || EntryIsVol(E)) {
            continue;
        }
        if (E[0] == '.' && (E[1] == ' ' || (E[1] == '.' && E[2] == ' '))) {
            continue;
        }
        return 1;
    }
    return 0;
}

int DirIsEmpty(FAT_DIR_CTX Dir) {
    if (Dir.IsFat16Root) {
        UINT32 s;
        for (s = 0; s < gRootSectors; s++) {
            if (!BlockReadSectors(gRootLba + s, 1, gSector)) {
                return 0;
            }
            if (DirBufferHasEntries(gSector, SECTOR)) {
                return 0;
            }
        }
        return 1;
    }

    {
        UINT32 Cluster = Dir.Cluster;
        while (!ClusterEnd(Cluster) && Cluster >= 2) {
            if (!LoadCluster(Cluster)) {
                return 0;
            }
            if (DirBufferHasEntries(gCluster, ClusterBytes())) {
                return 0;
            }
            Cluster = FatNext(Cluster);
        }
    }
    return 1;
}

int DirGetEntryPos(FAT_DIR_CTX Dir, UINT32 Index, UINT32 *Lba, UINT32 *Off) {
    UINT32 ByteOff = Index * 32;

    if (Dir.IsFat16Root) {
        if (ByteOff >= gRootSectors * SECTOR) {
            return 0;
        }
        *Lba = gRootLba + ByteOff / SECTOR;
        *Off = ByteOff % SECTOR;
        return 1;
    }
    {
        UINT32 Cluster = Dir.Cluster;
        UINT32 Cb = ClusterBytes();
        while (!ClusterEnd(Cluster) && Cluster >= 2) {
            if (ByteOff < Cb) {
                *Lba = gDataStart + (Cluster - 2) * gSectorsPerCluster + ByteOff / SECTOR;
                *Off = ByteOff % SECTOR;
                return 1;
            }
            ByteOff -= Cb;
            Cluster = FatNext(Cluster);
        }
    }
    return 0;
}

int DirReadEntry(FAT_DIR_CTX Dir, UINT32 Index, UINT8 Out[32]) {
    UINT32 Lba;
    UINT32 Off;
    int i;

    if (!DirGetEntryPos(Dir, Index, &Lba, &Off)) {
        return 0;
    }
    if (!LoadSector(Lba)) {
        return 0;
    }
    for (i = 0; i < 32; i++) {
        Out[i] = gSector[Off + i];
    }
    return 1;
}

int DirWriteEntry(FAT_DIR_CTX Dir, UINT32 Index, const UINT8 Ent[32]) {
    UINT32 Lba;
    UINT32 Off;
    int i;

    if (!DirGetEntryPos(Dir, Index, &Lba, &Off)) {
        return 0;
    }
    if (!LoadSector(Lba)) {
        return 0;
    }
    for (i = 0; i < 32; i++) {
        gSector[Off + i] = Ent[i];
    }
    return StoreSector(Lba);
}

int DirMaxIndex(FAT_DIR_CTX Dir) {
    if (Dir.IsFat16Root) {
        return (int)((gRootSectors * SECTOR) / 32);
    }
    {
        UINT32 Cluster = Dir.Cluster;
        UINT32 Total = 0;
        UINT32 Cb = ClusterBytes();
        while (!ClusterEnd(Cluster) && Cluster >= 2) {
            Total += Cb / 32;
            Cluster = FatNext(Cluster);
        }
        return (int)Total;
    }
}
