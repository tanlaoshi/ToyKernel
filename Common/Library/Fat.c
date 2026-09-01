/*
 * Fat.c — FAT16/FAT32 目录读写（8.3 短文件名，支持子路径）
 */
#include "Fat.h"
#include "Block.h"
#include "Console.h"
#include "Debug.h"

#define SECTOR 512
#define FAT32_EOC 0x0FFFFFF8u
#define FAT16_EOC 0xFFF8u
#define FAT_WRITE_MAX (64 * 1024)

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
static UINT32 gMaxCluster;

typedef struct {
    int      IsFat16Root;
    UINT32   Cluster;
} FAT_DIR_CTX;

static FAT_DIR_CTX FatRootCtx(void) {
    FAT_DIR_CTX C;
    if (gFatType == 16) {
        C.IsFat16Root = 1;
        C.Cluster = 0;
    } else {
        C.IsFat16Root = 0;
        C.Cluster = gRootCluster;
    }
    return C;
}

static UINT16 Read16(const UINT8 *P) {
    return (UINT16)P[0] | ((UINT16)P[1] << 8);
}

static UINT32 Read32(const UINT8 *P) {
    return (UINT32)P[0] | ((UINT32)P[1] << 8) |
           ((UINT32)P[2] << 16) | ((UINT32)P[3] << 24);
}

static void Write16(UINT8 *P, UINT16 V) {
    P[0] = (UINT8)(V & 0xFF);
    P[1] = (UINT8)((V >> 8) & 0xFF);
}

static void Write32(UINT8 *P, UINT32 V) {
    P[0] = (UINT8)(V & 0xFF);
    P[1] = (UINT8)((V >> 8) & 0xFF);
    P[2] = (UINT8)((V >> 16) & 0xFF);
    P[3] = (UINT8)((V >> 24) & 0xFF);
}

static int LoadSector(UINT32 Lba) {
    return BlockReadSectors(Lba, 1, gSector);
}

static int StoreSector(UINT32 Lba) {
    return BlockWriteSectors(Lba, 1, gSector);
}

static int LoadCluster(UINT32 Cluster) {
    UINT32 Lba = gDataStart + (Cluster - 2) * gSectorsPerCluster;
    return BlockReadSectors(Lba, gSectorsPerCluster, gCluster);
}

static int StoreCluster(UINT32 Cluster) {
    UINT32 Lba = gDataStart + (Cluster - 2) * gSectorsPerCluster;
    return BlockWriteSectors(Lba, gSectorsPerCluster, gCluster);
}

static UINT32 ClusterBytes(void) {
    return gBytesPerSector * gSectorsPerCluster;
}

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

static int FatSet(UINT32 Cluster, UINT32 Value) {
    UINT32 i;
    UINT32 Off;
    UINT32 Rel;

    if (Cluster < 2 || Cluster > gMaxCluster) {
        return 0;
    }
    if (gFatType == 32) {
        Rel = Cluster * 4;
        Off = Rel % SECTOR;
        for (i = 0; i < gNumFats; i++) {
            UINT32 FatLba = gFatStart + i * gSectorsPerFat + Rel / SECTOR;
            if (!LoadSector(FatLba)) {
                return 0;
            }
            Write32(gSector + Off, Value & 0x0FFFFFFFu);
            if (!StoreSector(FatLba)) {
                return 0;
            }
        }
        return 1;
    }
    Rel = Cluster * 2;
    Off = Rel % SECTOR;
    for (i = 0; i < gNumFats; i++) {
        UINT32 FatLba = gFatStart + i * gSectorsPerFat + Rel / SECTOR;
        if (!LoadSector(FatLba)) {
            return 0;
        }
        Write16(gSector + Off, (UINT16)Value);
        if (!StoreSector(FatLba)) {
            return 0;
        }
    }
    return 1;
}

static int ClusterEnd(UINT32 Cluster) {
    if (gFatType == 32) {
        return Cluster >= FAT32_EOC;
    }
    return Cluster >= FAT16_EOC;
}

static UINT32 EocValue(void) {
    return gFatType == 32 ? FAT32_EOC : FAT16_EOC;
}

static int NameEq(const UINT8 *Entry, const char *Name) {
    char FatName[13];
    int i;
    for (i = 0; i < 8 && Entry[i] != ' '; i++) {
        FatName[i] = (char)Entry[i];
    }
    {
        int Base = i;
        if (Entry[8] != ' ') {
            FatName[Base++] = '.';
            for (int j = 0; j < 3 && Entry[8 + j] != ' '; j++) {
                FatName[Base++] = (char)Entry[8 + j];
            }
        }
        FatName[Base] = 0;
    }

    {
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
}

/* Path -> 11 字节 8.3 目录名（大写，空格填充）；失败返回 0 */
static int PathTo83(const char *Path, UINT8 Out[11]) {
    int i;
    int bi = 0;
    int ei = 0;
    int seenDot = 0;

    for (i = 0; i < 11; i++) {
        Out[i] = ' ';
    }
    if (!Path || !Path[0]) {
        return 0;
    }
    for (i = 0; Path[i]; i++) {
        char C = Path[i];
        if (C == '.') {
            if (seenDot) {
                return 0;
            }
            seenDot = 1;
            continue;
        }
        if (C >= 'a' && C <= 'z') {
            C = (char)(C - 'a' + 'A');
        }
        if (!((C >= 'A' && C <= 'Z') || (C >= '0' && C <= '9') ||
              C == '_' || C == '-' || C == '~')) {
            return 0;
        }
        if (!seenDot) {
            if (bi >= 8) {
                return 0;
            }
            Out[bi++] = (UINT8)C;
        } else {
            if (ei >= 3) {
                return 0;
            }
            Out[8 + ei++] = (UINT8)C;
        }
    }
    return bi > 0 ? 1 : 0;
}

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

static int ScanDirBuffer(UINT8 *Buf, UINT32 Bytes, const char *Name,
                         UINT32 *OutCluster, UINT32 *OutSize, int ListOnly,
                         UINT8 *OutAttr) {
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
            if (OutAttr) {
                *OutAttr = E[11];
            }
            return 1;
        }
    }
    return ListOnly ? 1 : 0;
}

static int ForEachDir(FAT_DIR_CTX Dir, int ListOnly, const char *Name,
                      UINT32 *OutCluster, UINT32 *OutSize, UINT8 *OutAttr) {
    if (Dir.IsFat16Root) {
        for (UINT32 s = 0; s < gRootSectors; s++) {
            if (!BlockReadSectors(gRootLba + s, 1, gSector)) {
                return 0;
            }
            if (!ListOnly) {
                if (ScanDirBuffer(gSector, SECTOR, Name, OutCluster, OutSize, 0, OutAttr)) {
                    return 1;
                }
            } else if (!ScanDirBuffer(gSector, SECTOR, 0, 0, 0, 1, 0)) {
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
                    if (ScanDirBuffer(gCluster, Bytes, Name, OutCluster, OutSize, 0, OutAttr)) {
                        return 1;
                    }
                } else if (!ScanDirBuffer(gCluster, Bytes, 0, 0, 0, 1, 0)) {
                    return 0;
                }
            }
            Cluster = FatNext(Cluster);
        }
    }
    return ListOnly;
}


static int CopyPathComponent(const char **Path, char *Out, int OutMax) {
    const char *S = *Path;
    int n = 0;

    while (*S == '/') {
        S++;
    }
    if (!*S) {
        return 0;
    }
    while (*S && *S != '/') {
        char C;
        if (n + 1 >= OutMax) {
            return 0;
        }
        C = *S++;
        if (C >= 'a' && C <= 'z') {
            C = (char)(C - 'a' + 'A');
        }
        Out[n++] = C;
    }
    Out[n] = 0;
    *Path = S;
    return n > 0;
}

static int LookupInDir(FAT_DIR_CTX Dir, const char *Name,
                       UINT32 *OutCluster, UINT32 *OutSize, UINT8 *OutAttr) {
    return ForEachDir(Dir, 0, Name, OutCluster, OutSize, OutAttr);
}

static int ResolvePathAsDir(const char *Path, FAT_DIR_CTX *OutDir) {
    FAT_DIR_CTX Cur = FatRootCtx();
    char Comp[13];
    const char *P = Path;

    if (!Path || !Path[0]) {
        *OutDir = Cur;
        return 1;
    }
    while (CopyPathComponent(&P, Comp, sizeof(Comp))) {
        UINT32 SubCluster = 0;
        UINT32 SubSize = 0;
        UINT8 Attr = 0;

        if (!LookupInDir(Cur, Comp, &SubCluster, &SubSize, &Attr)) {
            return 0;
        }
        if (!(Attr & 0x10)) {
            return 0;
        }
        Cur.IsFat16Root = 0;
        Cur.Cluster = SubCluster;
        if (!*P || (*P == '/' && P[1] == 0)) {
            *OutDir = Cur;
            return 1;
        }
    }
    return 0;
}

static int ResolvePathParentLeaf(const char *Path, FAT_DIR_CTX *OutParent, char *Leaf) {
    FAT_DIR_CTX Cur = FatRootCtx();
    char Comp[13];
    const char *P = Path;
    char Last[13];
    int HasLast = 0;

    if (!Path || !Path[0]) {
        return 0;
    }
    while (CopyPathComponent(&P, Comp, sizeof(Comp))) {
        if (!*P || (*P == '/' && P[1] == 0)) {
            for (int i = 0; i < 13; i++) {
                Last[i] = Comp[i];
            }
            HasLast = 1;
            break;
        }
        {
            UINT32 SubCluster = 0;
            UINT32 SubSize = 0;
            UINT8 Attr = 0;
            if (!LookupInDir(Cur, Comp, &SubCluster, &SubSize, &Attr)) {
                return 0;
            }
            if (!(Attr & 0x10)) {
                return 0;
            }
            Cur.IsFat16Root = 0;
            Cur.Cluster = SubCluster;
        }
    }
    if (!HasLast) {
        return 0;
    }
    for (int i = 0; i < 13; i++) {
        Leaf[i] = Last[i];
    }
    *OutParent = Cur;
    return 1;
}

static int DirBufferHasEntries(UINT8 *Buf, UINT32 Bytes) {
    UINT32 Entries = Bytes / 32;
    for (UINT32 i = 0; i < Entries; i++) {
        UINT8 *E = Buf + i * 32;
        if (E[0] == 0x00) {
            return 0;
        }
        if (E[0] == 0xE5 || (E[11] & 0x08)) {
            continue;
        }
        return 1;
    }
    return 0;
}

static int DirIsEmpty(FAT_DIR_CTX Dir) {
    if (Dir.IsFat16Root) {
        for (UINT32 s = 0; s < gRootSectors; s++) {
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

/*
 * 在目录 Dir 中找 Name 的目录项，或空闲槽。
 * 找到后 gSector 含该扇区；写出 DirLba 与 EntryOff。
 */
static int FindDirSlot(FAT_DIR_CTX Dir, const char *Name, UINT8 Name83[11],
                       UINT32 *DirLba, UINT32 *EntryOff, int *FoundExisting,
                       UINT32 *OldCluster, UINT8 *OldAttr) {
    *FoundExisting = 0;
    *OldCluster = 0;
    if (OldAttr) {
        *OldAttr = 0;
    }

    if (Dir.IsFat16Root) {
        for (UINT32 s = 0; s < gRootSectors; s++) {
            UINT32 i;
            if (!LoadSector(gRootLba + s)) {
                return 0;
            }
            for (i = 0; i < SECTOR / 32; i++) {
                UINT8 *E = gSector + i * 32;
                if (E[0] == 0x00 || E[0] == 0xE5) {
                    *DirLba = gRootLba + s;
                    *EntryOff = i * 32;
                    return 1;
                }
                if ((E[11] & 0x08) || (E[11] & 0x10)) {
                    continue;
                }
                if (NameEq(E, Name)) {
                    *FoundExisting = 1;
                    *OldCluster = Read16(E + 26);
                    if (OldAttr) {
                        *OldAttr = E[11];
                    }
                    *DirLba = gRootLba + s;
                    *EntryOff = i * 32;
                    return 1;
                }
            }
        }
        (void)Name83;
        return 0;
    }

    {
        UINT32 Cluster = Dir.Cluster;
        while (!ClusterEnd(Cluster) && Cluster >= 2) {
            UINT32 Bytes;
            UINT32 Entries;
            UINT32 i;
            if (!LoadCluster(Cluster)) {
                return 0;
            }
            Bytes = ClusterBytes();
            Entries = Bytes / 32;
            for (i = 0; i < Entries; i++) {
                UINT8 *E = gCluster + i * 32;
                if (E[0] == 0x00 || E[0] == 0xE5) {
                    *DirLba = gDataStart + (Cluster - 2) * gSectorsPerCluster +
                              (i * 32) / SECTOR;
                    *EntryOff = (i * 32) % SECTOR;
                    if (!LoadSector(*DirLba)) {
                        return 0;
                    }
                    return 1;
                }
                if ((E[11] & 0x08) || (E[11] & 0x10)) {
                    continue;
                }
                if (NameEq(E, Name)) {
                    UINT32 Cl = Read16(E + 26);
                    if (gFatType == 32) {
                        Cl |= (UINT32)Read16(E + 20) << 16;
                    }
                    *FoundExisting = 1;
                    *OldCluster = Cl;
                    if (OldAttr) {
                        *OldAttr = E[11];
                    }
                    *DirLba = gDataStart + (Cluster - 2) * gSectorsPerCluster +
                              (i * 32) / SECTOR;
                    *EntryOff = (i * 32) % SECTOR;
                    if (!LoadSector(*DirLba)) {
                        return 0;
                    }
                    return 1;
                }
                (void)Name83;
            }
            Cluster = FatNext(Cluster);
        }
    }
    return 0;
}

static int FatFreeChain(UINT32 Cluster) {
    while (!ClusterEnd(Cluster) && Cluster >= 2 && Cluster <= gMaxCluster) {
        UINT32 Next = FatNext(Cluster);
        if (Next == 0xFFFFFFFFu) {
            return 0;
        }
        if (!FatSet(Cluster, 0)) {
            return 0;
        }
        Cluster = Next;
    }
    return 1;
}

static UINT32 FatAllocCluster(void) {
    UINT32 C;
    for (C = 2; C <= gMaxCluster; C++) {
        UINT32 V = FatNext(C);
        if (V == 0) {
            if (!FatSet(C, EocValue())) {
                return 0;
            }
            return C;
        }
    }
    return 0;
}

static int FindDirEntry(FAT_DIR_CTX Dir, const char *Name,
                        UINT32 *DirLba, UINT32 *EntryOff,
                        UINT32 *OutCluster, UINT8 *OutAttr) {
    if (Dir.IsFat16Root) {
        for (UINT32 s = 0; s < gRootSectors; s++) {
            if (!LoadSector(gRootLba + s)) {
                return 0;
            }
            for (UINT32 i = 0; i < SECTOR / 32; i++) {
                UINT8 *E = gSector + i * 32;
                if (E[0] == 0x00) {
                    return 0;
                }
                if (E[0] == 0xE5 || (E[11] & 0x08)) {
                    continue;
                }
                if (NameEq(E, Name)) {
                    *DirLba = gRootLba + s;
                    *EntryOff = i * 32;
                    *OutCluster = Read16(E + 26);
                    *OutAttr = E[11];
                    return 1;
                }
            }
        }
        return 0;
    }

    {
        UINT32 Cluster = Dir.Cluster;
        while (!ClusterEnd(Cluster) && Cluster >= 2) {
            if (!LoadCluster(Cluster)) {
                return 0;
            }
            {
                UINT32 Entries = ClusterBytes() / 32;
                for (UINT32 i = 0; i < Entries; i++) {
                    UINT8 *E = gCluster + i * 32;
                    if (E[0] == 0x00) {
                        return 0;
                    }
                    if (E[0] == 0xE5 || (E[11] & 0x08)) {
                        continue;
                    }
                    if (NameEq(E, Name)) {
                        *DirLba = gDataStart + (Cluster - 2) * gSectorsPerCluster +
                                  (i * 32) / SECTOR;
                        *EntryOff = (i * 32) % SECTOR;
                        *OutCluster = Read16(E + 26);
                        if (gFatType == 32) {
                            *OutCluster |= (UINT32)Read16(E + 20) << 16;
                        }
                        *OutAttr = E[11];
                        return 1;
                    }
                }
            }
            Cluster = FatNext(Cluster);
        }
    }
    return 0;
}

static int ReadFileClusters(UINT32 Cluster, UINT32 Size, void *Buffer, UINTN MaxSize,
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

int FatInit(UINT32 StartLba) {
    UINT32 TotSec;
    UINT32 DataSectors;

    gStartLba = StartLba;
    if (!LoadSector(StartLba)) {
        return 0;
    }
    gBytesPerSector = Read16(gSector + 11);
    gSectorsPerCluster = gSector[13];
    gReservedSectors = Read16(gSector + 14);
    gNumFats = gSector[16];
    gFatType = 0;

    if (gBytesPerSector != SECTOR || gSectorsPerCluster == 0 || gSectorsPerCluster > 128) {
        return 0;
    }

    TotSec = Read16(gSector + 19);
    if (TotSec == 0) {
        TotSec = Read32(gSector + 32);
    }

    if (gSector[82] == 'F' && gSector[83] == 'A' && gSector[84] == 'T') {
        gFatType = 32;
        gSectorsPerFat = Read32(gSector + 36);
        gRootCluster = Read32(gSector + 44);
        gFatStart = StartLba + gReservedSectors;
        gDataStart = gFatStart + gNumFats * gSectorsPerFat;
        DataSectors = TotSec ? (TotSec - (gDataStart - StartLba)) : (gSectorsPerFat * 128);
        gMaxCluster = 2 + DataSectors / gSectorsPerCluster - 1;
        if (gMaxCluster < 3) {
            gMaxCluster = 0xFFFF;
        }
        DebugWrite("FAT32 root cluster ");
        DebugHex32(gRootCluster);
        DebugWrite("\n");
        return 1;
    }

    gSectorsPerFat = Read16(gSector + 22);
    {
        UINT16 RootEntries = Read16(gSector + 17);
        gFatStart = StartLba + gReservedSectors;
        gRootLba = gFatStart + gNumFats * gSectorsPerFat;
        gRootSectors = ((UINT32)RootEntries * 32 + SECTOR - 1) / SECTOR;
        gDataStart = gRootLba + gRootSectors;
        DataSectors = TotSec ? (TotSec - (gDataStart - StartLba)) : (gSectorsPerFat * 256);
        gMaxCluster = 2 + DataSectors / gSectorsPerCluster - 1;
        if (gMaxCluster < 3) {
            gMaxCluster = 0xFFF0;
        }
    }
    gFatType = 16;
    DebugWrite("FAT16 root LBA ");
    DebugHex32(gRootLba);
    DebugWrite("\n");
    return 1;
}

int FatListRoot(void) {
    return FatListDir(0);
}

int FatListDir(const char *Path) {
    FAT_DIR_CTX Dir;

    if (!ResolvePathAsDir(Path ? Path : "", &Dir)) {
        ConsoleWrite("fat: dir not found\n");
        return 0;
    }
    return ForEachDir(Dir, 1, 0, 0, 0, 0);
}

int FatReadFile(const char *Path, void *Buffer, UINTN MaxSize, UINTN *OutSize) {
    FAT_DIR_CTX Parent;
    char Leaf[13];
    UINT32 Cluster = 0;
    UINT32 Size = 0;
    UINT8 Attr = 0;

    if (!Path || !Path[0]) {
        return 0;
    }
    if (!ResolvePathParentLeaf(Path, &Parent, Leaf)) {
        return 0;
    }
    if (!LookupInDir(Parent, Leaf, &Cluster, &Size, &Attr)) {
        return 0;
    }
    if (Attr & 0x10) {
        return 0;
    }
    return ReadFileClusters(Cluster, Size, Buffer, MaxSize, OutSize);
}

int FatWriteFile(const char *Path, const void *Buffer, UINTN Size) {
    FAT_DIR_CTX Parent;
    char Leaf[13];
    UINT8 Name83[11];
    UINT32 DirLba = 0;
    UINT32 EntryOff = 0;
    int Existing = 0;
    UINT32 OldCluster = 0;
    UINT32 FirstCluster = 0;
    UINT32 PrevCluster = 0;
    UINT32 NeedClusters;
    UINT32 Cb;
    UINT32 Written = 0;
    const UINT8 *Src = (const UINT8 *)Buffer;
    UINT8 *E;
    UINT32 i;

    if (!Path || (!Buffer && Size > 0)) {
        return 0;
    }
    if (Size > FAT_WRITE_MAX) {
        ConsoleWrite("fat: write too large (max 64K)\n");
        return 0;
    }
    if (!ResolvePathParentLeaf(Path, &Parent, Leaf)) {
        ConsoleWrite("fat: bad path\n");
        return 0;
    }
    if (!PathTo83(Leaf, Name83)) {
        ConsoleWrite("fat: bad 8.3 name\n");
        return 0;
    }
    if (!FindDirSlot(Parent, Leaf, Name83, &DirLba, &EntryOff, &Existing, &OldCluster, 0)) {
        ConsoleWrite("fat: no dir slot\n");
        return 0;
    }

    if (Existing && OldCluster >= 2) {
        if (!FatFreeChain(OldCluster)) {
            return 0;
        }
    }

    Cb = ClusterBytes();
    NeedClusters = Size == 0 ? 0 : (UINT32)((Size + Cb - 1) / Cb);

    for (i = 0; i < NeedClusters; i++) {
        UINT32 Cl = FatAllocCluster();
        UINT32 Chunk;
        if (Cl < 2) {
            ConsoleWrite("fat: no free cluster\n");
            if (FirstCluster >= 2) {
                FatFreeChain(FirstCluster);
            }
            return 0;
        }
        if (i == 0) {
            FirstCluster = Cl;
        } else if (!FatSet(PrevCluster, Cl)) {
            FatFreeChain(FirstCluster);
            return 0;
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
            return 0;
        }
        Written += Chunk;
    }

    /* 目录项：gSector 已是含槽位的扇区 */
    if (!LoadSector(DirLba)) {
        if (FirstCluster >= 2) {
            FatFreeChain(FirstCluster);
        }
        return 0;
    }
    E = gSector + EntryOff;
    for (i = 0; i < 11; i++) {
        E[i] = Name83[i];
    }
    E[11] = 0x20; /* archive */
    for (i = 12; i < 32; i++) {
        E[i] = 0;
    }
    if (gFatType == 32) {
        Write16(E + 20, (UINT16)((FirstCluster >> 16) & 0xFFFF));
    }
    Write16(E + 26, (UINT16)(FirstCluster & 0xFFFF));
    Write32(E + 28, (UINT32)Size);
    if (!StoreSector(DirLba)) {
        if (FirstCluster >= 2) {
            FatFreeChain(FirstCluster);
        }
        return 0;
    }
    return 1;
}

int FatDeleteFile(const char *Path) {
    FAT_DIR_CTX Parent;
    char Leaf[13];
    UINT32 DirLba = 0;
    UINT32 EntryOff = 0;
    UINT32 Cluster = 0;
    UINT8 Attr = 0;
    UINT8 *E;

    if (!Path || !Path[0]) {
        return 0;
    }
    if (!ResolvePathParentLeaf(Path, &Parent, Leaf)) {
        ConsoleWrite("fat: bad path\n");
        return 0;
    }
    if (!FindDirEntry(Parent, Leaf, &DirLba, &EntryOff, &Cluster, &Attr)) {
        ConsoleWrite("fat: not found\n");
        return 0;
    }
    if (Attr & 0x10) {
        FAT_DIR_CTX Sub = {0, Cluster};
        if (!DirIsEmpty(Sub)) {
            ConsoleWrite("fat: dir not empty\n");
            return 0;
        }
    }
    if (Cluster >= 2 && !FatFreeChain(Cluster)) {
        return 0;
    }
    if (!LoadSector(DirLba)) {
        return 0;
    }
    E = gSector + EntryOff;
    E[0] = 0xE5;
    return StoreSector(DirLba);
}
