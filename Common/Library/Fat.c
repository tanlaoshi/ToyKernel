/*
 * Fat.c — FAT16/FAT32 目录读写（8.3 + LFN，支持子路径）
 */
#include "Fat.h"
#include "Block.h"
#include "Console.h"
#include "Debug.h"

#define SECTOR 512
#define FAT32_EOC 0x0FFFFFF8u
#define FAT16_EOC 0xFFF8u
/* PR-FS3：与 Include/Fat.h 中 FAT_WRITE_MAX 保持一致 */
#ifndef FAT_WRITE_MAX
#define FAT_WRITE_MAX (8 * 1024 * 1024)
#endif
#define FAT_NAME_MAX 255
#define FAT_PATH_DEPTH 16
#define FAT_ATTR_RO   0x01
#define FAT_ATTR_HID  0x02
#define FAT_ATTR_SYS  0x04
#define FAT_ATTR_VOL  0x08
/* FAT_ATTR_DIR 见 Fat.h */
#define FAT_ATTR_ARCH 0x20
#define FAT_ATTR_LFN  0x0F

static FAT_DIR_ENT *gListOut;
static int gListMax;
static int gListCount;

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

typedef struct {
    char   Name[FAT_NAME_MAX + 1];
    int    Expect;   /* 下一期望 ordinal；0=无进行中的 LFN */
    UINT8  Cksum;
    int    Valid;
} FAT_LFN_ACC;

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

static char ToUpper(char C) {
    if (C >= 'a' && C <= 'z') {
        return (char)(C - 'a' + 'A');
    }
    return C;
}

static int StrEqIgnoreCase(const char *A, const char *B) {
    if (!A || !B) {
        return 0;
    }
    while (*A && *B) {
        if (ToUpper(*A) != ToUpper(*B)) {
            return 0;
        }
        A++;
        B++;
    }
    return *A == *B;
}

static int CompIsDot(const char *N) {
    return N && N[0] == '.' && N[1] == 0;
}

static int CompIsDotDot(const char *N) {
    return N && N[0] == '.' && N[1] == '.' && N[2] == 0;
}

static int CompIsDotOrDotDot(const char *N) {
    return CompIsDot(N) || CompIsDotDot(N);
}

static UINT8 Fat83Checksum(const UINT8 *Name83) {
    UINT8 Sum = 0;
    int i;
    for (i = 0; i < 11; i++) {
        Sum = (UINT8)(((Sum & 1) ? 0x80u : 0u) + (Sum >> 1) + Name83[i]);
    }
    return Sum;
}

static int EntryIsLfn(const UINT8 *E) {
    return (E[11] & FAT_ATTR_LFN) == FAT_ATTR_LFN;
}

static int EntryIsVol(const UINT8 *E) {
    return (E[11] & FAT_ATTR_VOL) && !EntryIsLfn(E);
}

static void ShortNameFromEntry(const UINT8 *E, char *Out, int OutMax) {
    int n = 0;
    int j;

    if (OutMax <= 0) {
        return;
    }
    for (j = 0; j < 8 && E[j] != ' '; j++) {
        if (n + 1 >= OutMax) {
            break;
        }
        Out[n++] = (char)E[j];
    }
    if (E[8] != ' ' && n + 1 < OutMax) {
        Out[n++] = '.';
        for (j = 0; j < 3 && E[8 + j] != ' '; j++) {
            if (n + 1 >= OutMax) {
                break;
            }
            Out[n++] = (char)E[8 + j];
        }
    }
    Out[n] = 0;
}

static int NameEqShort(const UINT8 *Entry, const char *Name) {
    char FatName[13];
    ShortNameFromEntry(Entry, FatName, sizeof(FatName));
    return StrEqIgnoreCase(FatName, Name);
}

/* Path -> 11 字节 8.3；失败返回 0（需走 LFN） */
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
        C = ToUpper(C);
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

static void LfnAccClear(FAT_LFN_ACC *A) {
    A->Name[0] = 0;
    A->Expect = 0;
    A->Cksum = 0;
    A->Valid = 0;
}

static void LfnPutUcs(FAT_LFN_ACC *A, int Index, UINT16 U) {
    if (Index < 0 || Index >= FAT_NAME_MAX) {
        return;
    }
    if (U == 0xFFFF) {
        return;
    }
    if (U == 0) {
        A->Name[Index] = 0;
        return;
    }
    if (U < 0x80) {
        A->Name[Index] = (char)U;
    } else if (U < 0x100) {
        A->Name[Index] = (char)U;
    } else {
        A->Name[Index] = '?';
    }
}

static int LfnFeed(FAT_LFN_ACC *A, const UINT8 *E) {
    int Ord = E[0] & 0x1F;
    int Base;
    int k;
    static const int Offs[13] = {
        1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30
    };

    if (Ord == 0) {
        LfnAccClear(A);
        return 1;
    }
    if (E[0] & 0x40) {
        LfnAccClear(A);
        A->Expect = Ord;
        A->Cksum = E[13];
        A->Valid = 1;
        for (k = 0; k <= FAT_NAME_MAX; k++) {
            A->Name[k] = 0;
        }
    } else if (!A->Valid || Ord != A->Expect || E[13] != A->Cksum) {
        LfnAccClear(A);
        return 1;
    }
    Base = (Ord - 1) * 13;
    for (k = 0; k < 13; k++) {
        UINT16 U = Read16(E + Offs[k]);
        LfnPutUcs(A, Base + k, U);
        if (U == 0) {
            break;
        }
    }
    A->Expect = Ord - 1;
    return 1;
}

static int EntryDisplayName(const UINT8 *E, const FAT_LFN_ACC *A, char *Out, int OutMax) {
    if (A && A->Valid && A->Expect == 0 && Fat83Checksum(E) == A->Cksum && A->Name[0]) {
        int i;
        for (i = 0; i < OutMax - 1 && A->Name[i]; i++) {
            Out[i] = A->Name[i];
        }
        Out[i] = 0;
        return 1;
    }
    ShortNameFromEntry(E, Out, OutMax);
    return 0;
}

static int EntryMatchesName(const UINT8 *E, const FAT_LFN_ACC *A, const char *Name) {
    char Disp[FAT_NAME_MAX + 1];

    if (NameEqShort(E, Name)) {
        return 1;
    }
    EntryDisplayName(E, A, Disp, sizeof(Disp));
    return StrEqIgnoreCase(Disp, Name);
}

static UINT32 EntryCluster(const UINT8 *E) {
    UINT32 Cl = Read16(E + 26);
    if (gFatType == 32) {
        Cl |= (UINT32)Read16(E + 20) << 16;
    }
    return Cl;
}

static void PrintEntryNamed(const char *Name, UINT8 Attr) {
    if (Attr & FAT_ATTR_DIR) {
        ConsoleWrite("[DIR] ");
    } else {
        ConsoleWrite("      ");
    }
    ConsoleWrite(Name);
    ConsoleWrite("\n");
}

static int ScanDirBuffer(UINT8 *Buf, UINT32 Bytes, const char *Name,
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

static int ForEachDir(FAT_DIR_CTX Dir, int ListOnly, const char *Name,
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
        if (n + 1 >= OutMax) {
            return 0;
        }
        Out[n++] = *S++;
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
    FAT_DIR_CTX Stack[FAT_PATH_DEPTH];
    int Sp = 0;
    char Comp[FAT_NAME_MAX + 1];
    const char *P = Path ? Path : "";

    Stack[0] = FatRootCtx();
    while (CopyPathComponent(&P, Comp, sizeof(Comp))) {
        UINT32 SubCluster = 0;
        UINT32 SubSize = 0;
        UINT8 Attr = 0;

        if (CompIsDot(Comp)) {
            continue;
        }
        if (CompIsDotDot(Comp)) {
            if (Sp > 0) {
                Sp--;
            }
            continue;
        }
        if (!LookupInDir(Stack[Sp], Comp, &SubCluster, &SubSize, &Attr)) {
            return 0;
        }
        if (!(Attr & FAT_ATTR_DIR)) {
            return 0;
        }
        if (Sp + 1 >= FAT_PATH_DEPTH) {
            return 0;
        }
        Sp++;
        Stack[Sp].IsFat16Root = 0;
        Stack[Sp].Cluster = SubCluster;
    }
    *OutDir = Stack[Sp];
    return 1;
}

static int ResolvePathParentLeaf(const char *Path, FAT_DIR_CTX *OutParent, char *Leaf) {
    FAT_DIR_CTX Stack[FAT_PATH_DEPTH];
    int Sp = 0;
    char Comp[FAT_NAME_MAX + 1];
    const char *P = Path;
    char Last[FAT_NAME_MAX + 1];
    int HasLast = 0;
    int i;

    if (!Path || !Path[0]) {
        return 0;
    }
    Stack[0] = FatRootCtx();
    Last[0] = 0;
    while (CopyPathComponent(&P, Comp, sizeof(Comp))) {
        if (!*P || (*P == '/' && P[1] == 0)) {
            for (i = 0; i <= FAT_NAME_MAX; i++) {
                Last[i] = Comp[i];
                if (!Comp[i]) {
                    break;
                }
            }
            HasLast = 1;
            break;
        }
        if (CompIsDot(Comp)) {
            continue;
        }
        if (CompIsDotDot(Comp)) {
            if (Sp > 0) {
                Sp--;
            }
            continue;
        }
        {
            UINT32 SubCluster = 0;
            UINT32 SubSize = 0;
            UINT8 Attr = 0;
            if (!LookupInDir(Stack[Sp], Comp, &SubCluster, &SubSize, &Attr)) {
                return 0;
            }
            if (!(Attr & FAT_ATTR_DIR)) {
                return 0;
            }
            if (Sp + 1 >= FAT_PATH_DEPTH) {
                return 0;
            }
            Sp++;
            Stack[Sp].IsFat16Root = 0;
            Stack[Sp].Cluster = SubCluster;
        }
    }
    if (!HasLast) {
        return 0;
    }
    for (i = 0; i <= FAT_NAME_MAX; i++) {
        Leaf[i] = Last[i];
        if (!Last[i]) {
            break;
        }
    }
    *OutParent = Stack[Sp];
    return 1;
}

static int DirBufferHasEntries(UINT8 *Buf, UINT32 Bytes) {
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

static int DirIsEmpty(FAT_DIR_CTX Dir) {
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

static int DirGetEntryPos(FAT_DIR_CTX Dir, UINT32 Index, UINT32 *Lba, UINT32 *Off) {
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

static int DirReadEntry(FAT_DIR_CTX Dir, UINT32 Index, UINT8 Out[32]) {
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

static int DirWriteEntry(FAT_DIR_CTX Dir, UINT32 Index, const UINT8 Ent[32]) {
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

static int DirMaxIndex(FAT_DIR_CTX Dir) {
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

static int NameIsDot(const UINT8 *E) {
    return E[0] == '.' && (E[1] == ' ' || (E[1] == '.' && E[2] == ' '));
}

const char *FatStrError(int Err) {
    switch (Err) {
    case FAT_OK:            return "ok";
    case FAT_ERR_IO:        return "I/O error";
    case FAT_ERR_NOENT:     return "not found";
    case FAT_ERR_NOSPC:     return "no space";
    case FAT_ERR_NOTDIR:    return "not a directory";
    case FAT_ERR_ISDIR:     return "is a directory";
    case FAT_ERR_NOTEMPTY:  return "directory not empty";
    case FAT_ERR_EXIST:     return "exists";
    case FAT_ERR_INVAL:     return "invalid";
    case FAT_ERR_NAMETOOLONG: return "name too long";
    case FAT_ERR_FBIG:      return "file too large";
    case FAT_ERR_ROFS:      return "read-only volume";
    default:                return "error";
    }
}

/*
 * 查找 Name 对应 SFN 下标，或 Need 个连续空闲槽起点。
 * 找到已存在时 *FoundExisting=1 且 *OutIndex 为 SFN；空闲时为第一槽。
 */
static int FindDirIndex(FAT_DIR_CTX Dir, const char *Name, int Need,
                        int *OutIndex, int *FoundExisting,
                        UINT32 *OldCluster, UINT8 *OldAttr) {
    FAT_LFN_ACC Acc;
    UINT8 E[32];
    int Max = DirMaxIndex(Dir);
    int i;
    int FreeRun = 0;
    int FreeStart = 0;

    *FoundExisting = 0;
    *OldCluster = 0;
    if (OldAttr) {
        *OldAttr = 0;
    }
    LfnAccClear(&Acc);

    for (i = 0; i < Max; i++) {
        if (!DirReadEntry(Dir, (UINT32)i, E)) {
            return 0;
        }
        if (E[0] == 0x00) {
            if (FreeRun == 0) {
                FreeStart = i;
            }
            FreeRun = Max - i;
            if (FreeRun >= Need) {
                *OutIndex = FreeStart;
                return 1;
            }
            return 0;
        }
        if (E[0] == 0xE5) {
            LfnAccClear(&Acc);
            if (FreeRun == 0) {
                FreeStart = i;
            }
            FreeRun++;
            if (FreeRun >= Need) {
                *OutIndex = FreeStart;
                return 1;
            }
            continue;
        }
        FreeRun = 0;
        if (EntryIsLfn(E)) {
            LfnFeed(&Acc, E);
            continue;
        }
        if (EntryIsVol(E) || NameIsDot(E)) {
            LfnAccClear(&Acc);
            continue;
        }
        if (EntryMatchesName(E, &Acc, Name)) {
            *FoundExisting = 1;
            *OutIndex = i;
            *OldCluster = EntryCluster(E);
            if (OldAttr) {
                *OldAttr = E[11];
            }
            return 1;
        }
        LfnAccClear(&Acc);
    }
    return 0;
}

static int LfnEntryCountForName(const char *Name) {
    int Len = 0;
    while (Name[Len]) {
        Len++;
    }
    if (Len == 0) {
        return 0;
    }
    return (Len + 12) / 13;
}

static void FillLfnEntry(UINT8 *E, int Ord, int IsLast, UINT8 Cksum, const char *Name) {
    static const int Offs[13] = {
        1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30
    };
    int Base = (Ord - 1) * 13;
    int k;
    int Ended = 0;

    for (k = 0; k < 32; k++) {
        E[k] = 0;
    }
    E[0] = (UINT8)(Ord | (IsLast ? 0x40 : 0));
    E[11] = FAT_ATTR_LFN;
    E[13] = Cksum;
    for (k = 0; k < 13; k++) {
        UINT16 U;
        if (Ended) {
            U = 0xFFFF;
        } else if (Name[Base + k] == 0) {
            U = 0;
            Ended = 1;
        } else {
            U = (UINT8)Name[Base + k];
        }
        Write16(E + Offs[k], U);
    }
}

/* 生成与长名对应的 8.3 别名 BASE~N.EXT */
static int Make83Alias(FAT_DIR_CTX Dir, const char *LongName, UINT8 Out[11]) {
    char Base[9];
    char Ext[4];
    int bi = 0;
    int ei = 0;
    int seenDot = 0;
    int i;
    int N;

    for (i = 0; i < 11; i++) {
        Out[i] = ' ';
    }
    Base[0] = 0;
    Ext[0] = 0;
    for (i = 0; LongName[i]; i++) {
        char C = ToUpper(LongName[i]);
        if (C == '.') {
            seenDot = 1;
            continue;
        }
        if (!((C >= 'A' && C <= 'Z') || (C >= '0' && C <= '9') ||
              C == '_' || C == '-')) {
            C = '_';
        }
        if (!seenDot) {
            if (bi < 8) {
                Base[bi++] = C;
            }
        } else if (ei < 3) {
            Ext[ei++] = C;
        }
    }
    Base[bi] = 0;
    Ext[ei] = 0;
    if (bi == 0) {
        Base[0] = 'X';
        Base[1] = 0;
        bi = 1;
    }

    for (N = 1; N <= 999999; N++) {
        char Trial[13];
        char Num[8];
        int ni = 0;
        int t;
        int Dig = N;
        int Prefix;
        UINT8 E[32];
        int Max;
        int Hit = 0;

        do {
            Num[ni++] = (char)('0' + (Dig % 10));
            Dig /= 10;
        } while (Dig > 0);
        Prefix = 8 - (1 + ni);
        if (Prefix < 1) {
            Prefix = 1;
        }
        if (Prefix > bi) {
            Prefix = bi;
        }
        for (i = 0; i < 11; i++) {
            Out[i] = ' ';
        }
        for (i = 0; i < Prefix; i++) {
            Out[i] = (UINT8)Base[i];
        }
        Out[Prefix] = '~';
        for (i = 0; i < ni; i++) {
            Out[Prefix + 1 + i] = (UINT8)Num[ni - 1 - i];
        }
        for (i = 0; i < ei && i < 3; i++) {
            Out[8 + i] = (UINT8)Ext[i];
        }

        ShortNameFromEntry(Out, Trial, sizeof(Trial));
        Max = DirMaxIndex(Dir);
        for (t = 0; t < Max; t++) {
            if (!DirReadEntry(Dir, (UINT32)t, E)) {
                break;
            }
            if (E[0] == 0x00) {
                break;
            }
            if (E[0] == 0xE5 || EntryIsLfn(E) || EntryIsVol(E)) {
                continue;
            }
            if (NameEqShort(E, Trial)) {
                Hit = 1;
                break;
            }
        }
        if (!Hit) {
            return 1;
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

static int DeleteLfnPrefix(FAT_DIR_CTX Dir, int SfnIndex, UINT8 Cksum) {
    int i;
    for (i = SfnIndex - 1; i >= 0; i--) {
        UINT8 E[32];
        int Last;
        if (!DirReadEntry(Dir, (UINT32)i, E)) {
            return 0;
        }
        if (!EntryIsLfn(E) || E[13] != Cksum) {
            break;
        }
        Last = (E[0] & 0x40) != 0;
        E[0] = 0xE5;
        if (!DirWriteEntry(Dir, (UINT32)i, E)) {
            return 0;
        }
        if (Last) {
            break;
        }
    }
    return 1;
}

static int FindFreeRun(FAT_DIR_CTX Dir, int Need, int *OutIndex) {
    int Max = DirMaxIndex(Dir);
    int FreeRun = 0;
    int FreeStart = 0;
    int t;

    for (t = 0; t < Max; t++) {
        UINT8 Tmp[32];
        if (!DirReadEntry(Dir, (UINT32)t, Tmp)) {
            return 0;
        }
        if (Tmp[0] == 0x00) {
            if (FreeRun == 0) {
                FreeStart = t;
            }
            if (Max - FreeStart >= Need) {
                *OutIndex = FreeStart;
                return 1;
            }
            return 0;
        }
        if (Tmp[0] == 0xE5) {
            if (FreeRun == 0) {
                FreeStart = t;
            }
            FreeRun++;
            if (FreeRun >= Need) {
                *OutIndex = FreeStart;
                return 1;
            }
        } else {
            FreeRun = 0;
        }
    }
    return 0;
}

/* FAT32/子目录：目录簇满时追加新簇；FAT16 根不可扩展 */
static int DirGrow(FAT_DIR_CTX Dir) {
    UINT32 Tail;
    UINT32 Next;
    UINT32 New;
    UINT32 z;
    UINT32 Cb;

    if (Dir.IsFat16Root) {
        return 0;
    }
    Tail = Dir.Cluster;
    if (Tail < 2) {
        return 0;
    }
    for (;;) {
        Next = FatNext(Tail);
        if (Next == 0xFFFFFFFFu) {
            return 0;
        }
        if (ClusterEnd(Next)) {
            break;
        }
        if (Next < 2) {
            return 0;
        }
        Tail = Next;
    }
    New = FatAllocCluster();
    if (New < 2) {
        return 0;
    }
    Cb = ClusterBytes();
    for (z = 0; z < Cb; z++) {
        gCluster[z] = 0;
    }
    if (!StoreCluster(New)) {
        FatSet(New, 0);
        return 0;
    }
    if (!FatSet(Tail, New)) {
        FatSet(New, 0);
        return 0;
    }
    return 1;
}

static int DirFindSlots(FAT_DIR_CTX Dir, int Need, int *OutIndex) {
    int Grow;

    for (Grow = 0; Grow < 8; Grow++) {
        if (FindFreeRun(Dir, Need, OutIndex)) {
            return 1;
        }
        if (!DirGrow(Dir)) {
            return 0;
        }
    }
    return 0;
}

static void FillSfnEntry(UINT8 *E, const UINT8 Name83[11], UINT8 Attr, UINT32 Cluster,
                         UINT32 Size) {
    UINT32 i;

    for (i = 0; i < 32; i++) {
        E[i] = 0;
    }
    for (i = 0; i < 11; i++) {
        E[i] = Name83[i];
    }
    E[11] = Attr;
    if (gFatType == 32) {
        Write16(E + 20, (UINT16)((Cluster >> 16) & 0xFFFF));
    }
    Write16(E + 26, (UINT16)(Cluster & 0xFFFF));
    Write32(E + 28, Size);
}

static UINT32 ParentClusterForDotDot(FAT_DIR_CTX Parent) {
    if (Parent.IsFat16Root) {
        return 0;
    }
    if (gFatType == 32 && Parent.Cluster == gRootCluster) {
        return 0;
    }
    return Parent.Cluster;
}

/* 在 Parent 中写入 LFN+SFN；Existing 时覆盖。成功 FAT_OK */
static int DirCreateEntry(FAT_DIR_CTX Parent, const char *Leaf, UINT8 Attr,
                          UINT32 Cluster, UINT32 Size) {
    UINT8 Name83[11];
    int LfnCount;
    int Need;
    int Index = 0;
    int Existing = 0;
    UINT32 OldCluster = 0;
    UINT8 OldAttr = 0;
    UINT8 E[32];
    UINT8 Cksum;
    int SfnIndex;
    int Ord;

    if (!Leaf || !Leaf[0] || CompIsDotOrDotDot(Leaf)) {
        return FAT_ERR_INVAL;
    }
    /*
     * 始终写 LFN：纯 8.3 短名会 ToUpper，QEMU vvfat 宿主侧又常显示成小写，
     * 重启后列表大小写与用户输入不一致。LFN 保留原始大小写。
     */
    if (!PathTo83(Leaf, Name83)) {
        if (!Make83Alias(Parent, Leaf, Name83)) {
            return FAT_ERR_NOSPC;
        }
    }
    LfnCount = LfnEntryCountForName(Leaf);
    if (LfnCount <= 0 || LfnCount > 20) {
        return FAT_ERR_NAMETOOLONG;
    }
    Need = LfnCount + 1;

    if (!FindDirIndex(Parent, Leaf, Need, &Index, &Existing, &OldCluster, &OldAttr)) {
        if (!DirFindSlots(Parent, Need, &Index)) {
            return FAT_ERR_NOSPC;
        }
        Existing = 0;
    }

    if (Existing) {
        UINT8 Old[32];
        UINT32 DeferredFree;

        if (!DirReadEntry(Parent, (UINT32)Index, Old)) {
            return FAT_ERR_IO;
        }
        /*
         * PR-FS3：先腾槽位再写新项，最后才释放旧簇。
         * 若在释放后 FindSlots/写项失败，会丢目录项。
         */
        DeferredFree = OldCluster;
        DeleteLfnPrefix(Parent, Index, Fat83Checksum(Old));
        Old[0] = 0xE5;
        if (!DirWriteEntry(Parent, (UINT32)Index, Old)) {
            return FAT_ERR_IO;
        }
        if (!DirFindSlots(Parent, Need, &Index)) {
            return FAT_ERR_NOSPC;
        }
        SfnIndex = Index + LfnCount;
        Cksum = Fat83Checksum(Name83);
        for (Ord = LfnCount; Ord >= 1; Ord--) {
            FillLfnEntry(E, Ord, Ord == LfnCount, Cksum, Leaf);
            if (!DirWriteEntry(Parent, (UINT32)(SfnIndex - Ord), E)) {
                return FAT_ERR_IO;
            }
        }
        FillSfnEntry(E, Name83, Attr, Cluster, Size);
        if (!DirWriteEntry(Parent, (UINT32)SfnIndex, E)) {
            return FAT_ERR_IO;
        }
        if (DeferredFree >= 2 && DeferredFree != Cluster) {
            FatFreeChain(DeferredFree);
        }
        return FAT_OK;
    }

    SfnIndex = Index + LfnCount;
    Cksum = Fat83Checksum(Name83);
    for (Ord = LfnCount; Ord >= 1; Ord--) {
        FillLfnEntry(E, Ord, Ord == LfnCount, Cksum, Leaf);
        if (!DirWriteEntry(Parent, (UINT32)(SfnIndex - Ord), E)) {
            return FAT_ERR_IO;
        }
    }
    FillSfnEntry(E, Name83, Attr, Cluster, Size);
    if (!DirWriteEntry(Parent, (UINT32)SfnIndex, E)) {
        return FAT_ERR_IO;
    }
    return FAT_OK;
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
        return FAT_ERR_IO;
    }
    gBytesPerSector = Read16(gSector + 11);
    gSectorsPerCluster = gSector[13];
    gReservedSectors = Read16(gSector + 14);
    gNumFats = gSector[16];
    gFatType = 0;

    if (gBytesPerSector != SECTOR || gSectorsPerCluster == 0 || gSectorsPerCluster > 128) {
        return FAT_ERR_INVAL;
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
        return FAT_OK;
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
    return FAT_OK;
}

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

int FatListEntries(const char *Path, FAT_DIR_ENT *Out, int Max, int *OutCount) {
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
        return FAT_ERR_FBIG;
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
     * 先写全新簇链，目录项仍指向旧文件；失败只回滚新链，旧项保留（PR-FS3）。
     */
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
    }

    if (Existing) {
        UINT8 E[32];

        /* 同名覆盖：就地改 SFN 的簇/大小，再释放旧链 */
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
        if (OldCluster >= 2 && OldCluster != FirstCluster) {
            FatFreeChain(OldCluster);
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

int FatDeleteFile(const char *Path) {
    FAT_DIR_CTX Parent;
    char Leaf[FAT_NAME_MAX + 1];
    int Index = 0;
    int Existing = 0;
    UINT32 Cluster = 0;
    UINT8 Attr = 0;
    UINT8 E[32];

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
    if (!DirReadEntry(Parent, (UINT32)Index, E)) {
        return FAT_ERR_IO;
    }
    DeleteLfnPrefix(Parent, Index, Fat83Checksum(E));
    if (Cluster >= 2 && !FatFreeChain(Cluster)) {
        return FAT_ERR_IO;
    }
    E[0] = 0xE5;
    if (!DirWriteEntry(Parent, (UINT32)Index, E)) {
        return FAT_ERR_IO;
    }
    return FAT_OK;
}

/* 仅摘掉目录项，不释放簇（供 Rename） */
static int DirUnlinkKeepClusters(FAT_DIR_CTX Parent, const char *Leaf) {
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
