/*
 * Fat.c — FAT16/FAT32 只读实现
 *
 * 解析 BPB、遍历根目录、按簇链读取文件。缓冲区使用静态数组，无动态分配。
 */
#include "Fat.h"
#include "Ata.h"
#include "Console.h"
#include "Debug.h"

#define SECTOR 512
#define FAT32_EOC 0x0FFFFFF8u
#define FAT16_EOC 0xFFF8u

static UINT8 gSector[SECTOR];
static UINT8 gCluster[SECTOR * 128];

static UINT32 gStartLba;
static UINT32 gBytesPerSector;
static UINT32 gSectorsPerCluster;
static UINT32 gReservedSectors;
static UINT32 gNumFats;
static UINT32 gSectorsPerFat;
static UINT32 gRootCluster;
static UINT32 gRootLba;
static UINT32 gRootSectors;
static UINT32 gFatStart;
static UINT32 gDataStart;
static UINT32 gFatType;

/* 从字节数组读取小端 UINT16 */
static UINT16 Read16(const UINT8 *P) {
    return (UINT16)P[0] | ((UINT16)P[1] << 8);
}

/* 从字节数组读取小端 UINT32 */
static UINT32 Read32(const UINT8 *P) {
    return (UINT32)P[0] | ((UINT32)P[1] << 8) |
           ((UINT32)P[2] << 16) | ((UINT32)P[3] << 24);
}

/* 将全局扇区缓冲加载为指定 LBA 的 512 字节 */
static int LoadSector(UINT32 Lba) {
    return AtaReadSectors(Lba, 1, gSector);
}

/* 将整个簇读入 gCluster 缓冲 */
static int LoadCluster(UINT32 Cluster) {
    UINT32 Lba = gDataStart + (Cluster - 2) * gSectorsPerCluster;
    return AtaReadSectors(Lba, gSectorsPerCluster, gCluster);
}

/* 查 FAT 表获取下一簇号 */
static UINT32 FatNext(UINT32 Cluster) {
    UINT32 FatLba;
    UINT32 Off;
    if (gFatType == 32) {
        FatLba = gFatStart + (Cluster * 4) / SECTOR;
        Off = (Cluster * 4) % SECTOR;
        if (!LoadSector(FatLba)) {
            return 0xFFFFFFFFu;
        }
        return Read32(gSector + Off) & 0x0FFFFFFFu;
    }
    FatLba = gFatStart + (Cluster * 2) / SECTOR;
    Off = (Cluster * 2) % SECTOR;
    if (!LoadSector(FatLba)) {
        return 0xFFFFFFFFu;
    }
    return Read16(gSector + Off);
}

/* 判断簇号是否表示链结束（FAT16/FAT32 EOC 阈值不同） */
static int ClusterEnd(UINT32 Cluster) {
    if (gFatType == 32) {
        return Cluster >= FAT32_EOC;
    }
    return Cluster >= FAT16_EOC;
}

/* 比较目录项 8.3 名与给定路径名（不区分大小写） */
static int NameEq(const UINT8 *Entry, const char *Name) {
    char FatName[13];
    int i;
    for (i = 0; i < 8 && Entry[i] != ' '; i++) {
        FatName[i] = (char)Entry[i];
    }
    int Base = i;
    if (Entry[8] != ' ') {
        FatName[Base++] = '.';
        for (int j = 0; j < 3 && Entry[8 + j] != ' '; j++) {
            FatName[Base++] = (char)Entry[8 + j];
        }
    }
    FatName[Base] = 0;

    const char *A = FatName;
    const char *B = Name;
    while (*A && *B) {
        char Ca = *A;
        char Cb = *B;
        if (Ca >= 'a' && Ca <= 'z') {
            Ca = (char)(Ca - 'a' + 'A');
        }
        if (Cb >= 'a' && Cb <= 'z') {
            Cb = (char)(Cb - 'a' + 'A');
        }
        if (Ca != Cb) {
            return 0;
        }
        A++;
        B++;
    }
    return *A == *B;
}

/* 格式化打印一条目录项（目录加 [DIR] 前缀） */
static void PrintEntry(UINT8 *E) {
    char Name[13];
    int n = 0;
    for (int j = 0; j < 8 && E[j] != ' '; j++) {
        Name[n++] = (char)E[j];
    }
    if (E[8] != ' ') {
        Name[n++] = '.';
        for (int j = 0; j < 3 && E[8 + j] != ' '; j++) {
            Name[n++] = (char)E[8 + j];
        }
    }
    Name[n] = 0;
    if (E[11] & 0x10) {
        ConsoleWrite("[DIR] ");
    } else {
        ConsoleWrite("      ");
    }
    ConsoleWrite(Name);
    ConsoleWrite("\n");
}

/* 扫描目录缓冲区：ListOnly=1 列举，否则按 Name 查找并返回簇号与大小 */
static int ScanDirBuffer(UINT8 *Buf, UINT32 Bytes, const char *Name,
                         UINT32 *OutCluster, UINT32 *OutSize, int ListOnly) {
    UINT32 Entries = Bytes / 32;
    for (UINT32 i = 0; i < Entries; i++) {
        UINT8 *E = Buf + i * 32;
        if (E[0] == 0x00) {
            return ListOnly ? 1 : 0;
        }
        if (E[0] == 0xE5 || (E[11] & 0x08)) {
            continue;
        }
        if (ListOnly) {
            PrintEntry(E);
            continue;
        }
        if (NameEq(E, Name)) {
            UINT32 Cl = Read16(E + 26);
            if (gFatType == 32) {
                Cl |= (UINT32)Read16(E + 20) << 16;
            }
            *OutCluster = Cl;
            *OutSize = Read32(E + 28);
            return 1;
        }
    }
    return ListOnly ? 1 : 0;
}

/* 遍历根目录（FAT32 按簇链，FAT16 按固定扇区） */
static int ForEachRoot(int ListOnly, const char *Name,
                       UINT32 *OutCluster, UINT32 *OutSize) {
    if (gFatType == 32) {
        UINT32 Cluster = gRootCluster;
        while (!ClusterEnd(Cluster) && Cluster >= 2) {
            if (!LoadCluster(Cluster)) {
                return 0;
            }
            UINT32 Bytes = gBytesPerSector * gSectorsPerCluster;
            if (!ListOnly) {
                if (ScanDirBuffer(gCluster, Bytes, Name, OutCluster, OutSize, 0)) {
                    return 1;
                }
            } else if (!ScanDirBuffer(gCluster, Bytes, 0, 0, 0, 1)) {
                return 0;
            }
            Cluster = FatNext(Cluster);
        }
        return ListOnly;
    }

    for (UINT32 s = 0; s < gRootSectors; s++) {
        if (!AtaReadSectors(gRootLba + s, 1, gSector)) {
            return 0;
        }
        if (!ListOnly) {
            if (ScanDirBuffer(gSector, SECTOR, Name, OutCluster, OutSize, 0)) {
                return 1;
            }
        } else if (!ScanDirBuffer(gSector, SECTOR, 0, 0, 0, 1)) {
            return 0;
        }
    }
    return ListOnly;
}

/* 从 StartLba 读取 BPB 并初始化全局 FAT 参数 */
int FatInit(UINT32 StartLba) {
    gStartLba = StartLba;
    if (!LoadSector(StartLba)) {
        return 0;
    }
    gBytesPerSector = Read16(gSector + 11);
    gSectorsPerCluster = gSector[13];
    gReservedSectors = Read16(gSector + 14);
    gNumFats = gSector[16];
    gFatType = 0;

    if (gBytesPerSector != SECTOR || gSectorsPerCluster == 0) {
        return 0;
    }

    if (gSector[82] == 'F' && gSector[83] == 'A' && gSector[84] == 'T') {
        gFatType = 32;
        gSectorsPerFat = Read32(gSector + 36);
        gRootCluster = Read32(gSector + 44);
        gFatStart = StartLba + gReservedSectors;
        gDataStart = gFatStart + gNumFats * gSectorsPerFat;
        DebugWrite("FAT32 root cluster ");
        DebugHex32(gRootCluster);
        DebugWrite("\n");
        return 1;
    }

    gSectorsPerFat = Read16(gSector + 22);
    UINT16 RootEntries = Read16(gSector + 17);
    gFatStart = StartLba + gReservedSectors;
    gRootLba = gFatStart + gNumFats * gSectorsPerFat;
    gRootSectors = ((UINT32)RootEntries * 32 + SECTOR - 1) / SECTOR;
    gDataStart = gRootLba + gRootSectors;
    gFatType = 16;
    DebugWrite("FAT16 root LBA ");
    DebugHex32(gRootLba);
    DebugWrite(" sectors ");
    DebugHex32(gRootSectors);
    DebugWrite("\n");
    return 1;
}

/* 列出根目录所有条目 */
int FatListRoot(void) {
    return ForEachRoot(1, 0, 0, 0);
}

/* 按 8.3 路径名读取文件到 Buffer，最多 MaxSize 字节 */
int FatReadFile(const char *Path, void *Buffer, UINTN MaxSize, UINTN *OutSize) {
    UINT32 Cluster = 0;
    UINT32 Size = 0;
    if (!ForEachRoot(0, Path, &Cluster, &Size)) {
        return 0;
    }
    UINT8 *Dst = (UINT8 *)Buffer;
    UINTN Total = 0;
    UINT32 Remaining = Size;
    while (!ClusterEnd(Cluster) && Cluster >= 2 && Total < MaxSize) {
        if (!LoadCluster(Cluster)) {
            return 0;
        }
        UINT32 Chunk = gBytesPerSector * gSectorsPerCluster;
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
        Cluster = FatNext(Cluster);
    }
    if (OutSize) {
        *OutSize = Total;
    }
    return 1;
}
