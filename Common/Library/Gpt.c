/*
 * Gpt.c — MBR / GPT 分区表解析
 *
 * 读取 LBA0 判断启动方式，在分区表中查找 FAT12/16/32 类型分区。
 */
#include "Gpt.h"
#include "Block.h"
#include "Debug.h"

#define SECTOR 512

static UINT8 gSector[SECTOR];

/* 检查扇区末尾 0x55AA 引导签名 */
static int SectorHasBootSig(void) {
    return gSector[510] == 0x55 && gSector[511] == 0xAA;
}

/* 判断缓冲区是否为 FAT 引导扇区（superfloppy / 分区起点） */
static int IsFatBootSector(void) {
    if (!SectorHasBootSig()) {
        return 0;
    }
    if (gSector[0] != 0xEB && gSector[0] != 0xE9) {
        return 0;
    }
    if (gSector[82] == 'F' && gSector[83] == 'A' && gSector[84] == 'T') {
        return 1;
    }
    if (gSector[54] == 'F' && gSector[55] == 'A' && gSector[56] == 'T') {
        return 1;
    }
    return 0;
}

static int LbaLooksLikeFat(UINT32 Lba) {
    if (!BlockReadSectors(Lba, 1, gSector)) {
        return 0;
    }
    return IsFatBootSector();
}

static int AddPart(GPT_FAT_PART *Out, int Max, int Count, UINT32 Lba, int IsEsp) {
    int i;

    if (Count >= Max || !Out) {
        return Count;
    }
    for (i = 0; i < Count; i++) {
        if (Out[i].StartLba == Lba) {
            if (IsEsp) {
                Out[i].IsEsp = 1;
            }
            return Count;
        }
    }
    Out[Count].StartLba = Lba;
    Out[Count].IsEsp = IsEsp ? 1 : 0;
    return Count + 1;
}

/* 解析 MBR 分区表：收集全部 FAT 类型分区；返回 2=GPT 保护分区 */
static int MbrCollectFat(GPT_FAT_PART *Out, int Max, int *InOutCount) {
    int SawGpt = 0;
    int i;

    if (!SectorHasBootSig()) {
        return 0;
    }
    for (i = 0; i < 4; i++) {
        UINT8 *P = gSector + 0x1BE + i * 16;
        UINT8 Type = P[4];
        UINT32 Start;

        if (Type == 0x00) {
            continue;
        }
        if (Type == 0xEE) {
            SawGpt = 1;
            continue;
        }
        /* 0xEF = EFI System（多数可启动 U 盘 MBR ESP）；FAT 类型一并挂 */
        if (Type == 0x0B || Type == 0x0C || Type == 0x0E || Type == 0x06 ||
            Type == 0x04 || Type == 0xEF) {
            int IsEsp = (Type == 0xEF) ? 1 : 0;
            Start = (UINT32)P[8] | ((UINT32)P[9] << 8) |
                    ((UINT32)P[10] << 16) | ((UINT32)P[11] << 24);
            *InOutCount = AddPart(Out, Max, *InOutCount, Start, IsEsp);
        }
    }
    return SawGpt ? 2 : (*InOutCount > 0 ? 1 : 0);
}

static int GuidEq(const UINT8 *A, const UINT8 *B) {
    int i;

    for (i = 0; i < 16; i++) {
        if (A[i] != B[i]) {
            return 0;
        }
    }
    return 1;
}

static const UINT8 EFI_PART[] = {
    0x28, 0x73, 0x2A, 0xC1, 0x1F, 0xF8, 0xD2, 0x11,
    0xBA, 0x4B, 0x00, 0xA0, 0xC9, 0x3E, 0xC9, 0x3B
};

static const UINT8 BASIC_DATA[] = {
    0xA2, 0xA0, 0xD0, 0xEB, 0xE5, 0xB9, 0x33, 0x44,
    0x87, 0xC0, 0x68, 0xB6, 0xB7, 0x26, 0x99, 0xC7
};

/*
 * 枚举 GPT：EFI System + Microsoft Basic Data；
 * 其它 GUID 若首扇区像 FAT 也收录（避免类型标错漏挂 TOYOS）。
 */
static int GptCollectFat(GPT_FAT_PART *Out, int Max, int *InOutCount) {
    if (!BlockReadSectors(1, 1, gSector)) {
        return 0;
    }
    if (*(UINT64 *)(void *)gSector != 0x5452415020494645ULL) {
        return 0;
    }
    {
        UINT32 EntriesLba = *(UINT32 *)(void *)(gSector + 72);
        UINT32 EntryCount = *(UINT32 *)(void *)(gSector + 80);
        UINT32 EntrySize = *(UINT32 *)(void *)(gSector + 84);
        UINT8 Entry[128];
        UINT32 i;

        if (EntrySize < 128 || EntryCount == 0 || EntryCount > 128) {
            return 0;
        }
        for (i = 0; i < EntryCount; i++) {
            UINT32 Sec = EntriesLba + (i * EntrySize) / SECTOR;
            UINT32 Off = (i * EntrySize) % SECTOR;
            UINT32 j;
            UINT64 First;
            UINT64 Last;
            int IsEsp;
            int Want;

            if (Off + 128 > SECTOR) {
                continue; /* 跨扇区项少见；跳过避免读穿 */
            }
            if (!BlockReadSectors(Sec, 1, gSector)) {
                return 0;
            }
            for (j = 0; j < 128; j++) {
                Entry[j] = gSector[Off + j];
            }
            First = *(UINT64 *)(void *)(Entry + 32);
            Last = *(UINT64 *)(void *)(Entry + 40);
            if (First == 0 && Last == 0) {
                continue;
            }
            IsEsp = GuidEq(Entry, EFI_PART);
            Want = IsEsp || GuidEq(Entry, BASIC_DATA);
            if (!Want) {
                /* GUID 未识别：仍探测是否 FAT（真机偶发类型非 0700） */
                if (!LbaLooksLikeFat((UINT32)First)) {
                    continue;
                }
            } else if (!IsEsp) {
                /* Basic Data 也可能是 NTFS；非 FAT 则跳过 */
                if (!LbaLooksLikeFat((UINT32)First)) {
                    continue;
                }
            } else {
                /* ESP 几乎总是 FAT；失败则跳过 */
                if (!LbaLooksLikeFat((UINT32)First)) {
                    continue;
                }
            }
            *InOutCount = AddPart(Out, Max, *InOutCount, (UINT32)First, IsEsp);
        }
    }
    return *InOutCount > 0 ? 1 : 0;
}

int GptFindAllFat(GPT_FAT_PART *Out, int Max) {
    int Count = 0;
    int Mbr;

    if (!Out || Max <= 0) {
        return 0;
    }
    if (!BlockReadSectors(0, 1, gSector)) {
        return 0;
    }
    if (IsFatBootSector()) {
        return AddPart(Out, Max, 0, 0, 0);
    }
    Mbr = MbrCollectFat(Out, Max, &Count);
    if (Mbr == 2) {
        Count = 0;
        if (!GptCollectFat(Out, Max, &Count)) {
            DebugWrite("FS: GPT but no FAT partitions\n");
            return 0;
        }
    } else if (Count <= 0) {
        DebugWrite("FS: no FAT partition found\n");
        return 0;
    }
    {
        int i;
        for (i = 0; i < Count; i++) {
            DebugWrite("FS: FAT LBA ");
            DebugHex32(Out[i].StartLba);
            if (Out[i].IsEsp) {
                DebugWrite(" (ESP)");
            }
            DebugWrite("\n");
        }
    }
    return Count;
}

int GptFindFatStartEx(UINT32 *OutLba, int *OutIsEsp) {
    GPT_FAT_PART Parts[GPT_MAX_FAT_PARTS];
    int N;
    int i;
    int Prefer = -1;

    if (OutIsEsp) {
        *OutIsEsp = 0;
    }
    if (!OutLba) {
        return 0;
    }
    N = GptFindAllFat(Parts, GPT_MAX_FAT_PARTS);
    if (N <= 0) {
        return 0;
    }
    /* 兼容旧语义：优先非 ESP（TOYOS/数据卷），否则 ESP */
    for (i = 0; i < N; i++) {
        if (!Parts[i].IsEsp) {
            Prefer = i;
            break;
        }
    }
    if (Prefer < 0) {
        Prefer = 0;
    }
    *OutLba = Parts[Prefer].StartLba;
    if (OutIsEsp) {
        *OutIsEsp = Parts[Prefer].IsEsp;
    }
    return 1;
}

int GptFindFatStart(UINT32 *OutLba) {
    return GptFindFatStartEx(OutLba, 0);
}

/* —— PR-FS-inst-1：写 GPT —— */

static UINT32 GptCrc32(const UINT8 *Data, UINTN Len) {
    UINT32 C = 0xFFFFFFFFu;
    UINTN i;
    int b;

    for (i = 0; i < Len; i++) {
        C ^= Data[i];
        for (b = 0; b < 8; b++) {
            if (C & 1u) {
                C = (C >> 1) ^ 0xEDB88320u;
            } else {
                C >>= 1;
            }
        }
    }
    return ~C;
}

static void ZeroSector(void) {
    int i;
    for (i = 0; i < SECTOR; i++) {
        gSector[i] = 0;
    }
}

static void PutGuid(UINT8 *Dst, const UINT8 *Src) {
    int i;
    for (i = 0; i < 16; i++) {
        Dst[i] = Src[i];
    }
}

static void PutUtf16Name(UINT8 *Dst, const char *Ascii) {
    int i;
    for (i = 0; i < 36; i++) {
        Dst[i * 2] = 0;
        Dst[i * 2 + 1] = 0;
    }
    for (i = 0; Ascii[i] && i < 35; i++) {
        Dst[i * 2] = (UINT8)Ascii[i];
        Dst[i * 2 + 1] = 0;
    }
}

static void Write64(UINT8 *P, UINT64 V) {
    int i;
    for (i = 0; i < 8; i++) {
        P[i] = (UINT8)((V >> (8 * i)) & 0xFF);
    }
}


static void Write32Le(UINT8 *P, UINT32 V) {
    P[0] = (UINT8)(V & 0xFF);
    P[1] = (UINT8)((V >> 8) & 0xFF);
    P[2] = (UINT8)((V >> 16) & 0xFF);
    P[3] = (UINT8)((V >> 24) & 0xFF);
}

int GptWriteToyLayout(UINT64 TotalSectors, UINT32 EspMib,
                      UINT32 *OutEspLba, UINT32 *OutEspSectors,
                      UINT32 *OutToyLba, UINT32 *OutToySectors) {
    UINT32 EspLba = 2048; /* 1MiB */
    UINT32 EspSectors;
    UINT32 ToyLba;
    UINT32 ToySectors;
    UINT64 FirstUsable = 34;
    UINT64 LastUsable;
    UINT64 BackupLba;
    UINT64 EntriesLba = 2;
    UINT64 EntriesBackupLba;
    static UINT8 Entries[32 * SECTOR];
    static UINT8 Hdr[SECTOR];
    UINT32 EntriesCrc;
    UINT32 HdrCrc;
    UINT32 i;
    static const UINT8 DiskGuid[16] = {
        0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC, 0xDE, 0xF0,
        0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88
    };
    static const UINT8 EspPartGuid[16] = {
        0xAA, 0xBB, 0xCC, 0xDD, 0x01, 0x02, 0x03, 0x04,
        0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C
    };
    static const UINT8 ToyPartGuid[16] = {
        0xDE, 0xAD, 0xBE, 0xEF, 0x10, 0x20, 0x30, 0x40,
        0x50, 0x60, 0x70, 0x80, 0x90, 0xA0, 0xB0, 0xC0
    };

    if (EspMib < 32) {
        EspMib = 256;
    }
    if (TotalSectors < (UINT64)(EspLba + EspMib * 2048ull + 2048ull + 34ull)) {
        DebugWrite("gpt: disk too small\n");
        return 0;
    }
    DebugWrite("gpt: total="); DebugHex32((UINT32)TotalSectors); DebugWrite("\n");
    EspSectors = EspMib * 2048u;
    ToyLba = EspLba + EspSectors;
    LastUsable = TotalSectors - 34ull;
    if ((UINT64)ToyLba >= LastUsable) {
        return 0;
    }
    ToySectors = (UINT32)(LastUsable - (UINT64)ToyLba + 1ull);
    BackupLba = TotalSectors - 1ull;
    EntriesBackupLba = TotalSectors - 33ull;

    /* Protective MBR */
    ZeroSector();
    gSector[0x1BE] = 0x00;
    gSector[0x1BE + 1] = 0x00;
    gSector[0x1BE + 2] = 0x02;
    gSector[0x1BE + 3] = 0x00;
    gSector[0x1BE + 4] = 0xEE;
    gSector[0x1BE + 5] = 0xFF;
    gSector[0x1BE + 6] = 0xFF;
    gSector[0x1BE + 7] = 0xFF;
    Write32Le(gSector + 0x1BE + 8, 1);
    Write32Le(gSector + 0x1BE + 12, (UINT32)(TotalSectors > 0xFFFFFFFFull ? 0xFFFFFFFFu : (TotalSectors - 1ull)));
    gSector[510] = 0x55;
    gSector[511] = 0xAA;
    if (!BlockWriteSectors(0, 1, gSector)) {
        DebugWrite("gpt: MBR write fail\n");
        return 0;
    }

    /* Partition entries (2 × 128B used; rest zero) */
    for (i = 0; i < sizeof(Entries); i++) {
        Entries[i] = 0;
    }
    PutGuid(Entries + 0, EFI_PART);
    PutGuid(Entries + 16, EspPartGuid);
    Write64(Entries + 32, EspLba);
    Write64(Entries + 40, (UINT64)EspLba + EspSectors - 1ull);
    PutUtf16Name(Entries + 56, "ESP");

    PutGuid(Entries + 128, BASIC_DATA);
    PutGuid(Entries + 128 + 16, ToyPartGuid);
    Write64(Entries + 128 + 32, ToyLba);
    Write64(Entries + 128 + 40, (UINT64)ToyLba + ToySectors - 1ull);
    PutUtf16Name(Entries + 128 + 56, "TOYOS");

    EntriesCrc = GptCrc32(Entries, sizeof(Entries));
    for (i = 0; i < 32; i++) {
        if (!BlockWriteSectors((UINT32)(EntriesLba + i), 1, Entries + i * SECTOR)) {
            DebugWrite("gpt: entries write fail\n");
            return 0;
        }
    }
    for (i = 0; i < 32; i++) {
        if (!BlockWriteSectors((UINT32)(EntriesBackupLba + i), 1, Entries + i * SECTOR)) {
            DebugWrite("gpt: backup entries fail\n");
            return 0;
        }
    }

    /* Primary header LBA1 */
    for (i = 0; i < SECTOR; i++) {
        Hdr[i] = 0;
    }
    /* Signature "EFI PART" */
    Hdr[0] = 'E'; Hdr[1] = 'F'; Hdr[2] = 'I'; Hdr[3] = ' ';
    Hdr[4] = 'P'; Hdr[5] = 'A'; Hdr[6] = 'R'; Hdr[7] = 'T';
    Write32Le(Hdr + 8, 0x00010000);
    Write32Le(Hdr + 12, 92);
    Write32Le(Hdr + 16, 0); /* CRC placeholder */
    Write64(Hdr + 24, 1);
    Write64(Hdr + 32, BackupLba);
    Write64(Hdr + 40, FirstUsable);
    Write64(Hdr + 48, LastUsable);
    PutGuid(Hdr + 56, DiskGuid);
    Write64(Hdr + 72, EntriesLba);
    Write32Le(Hdr + 80, 128);
    Write32Le(Hdr + 84, 128);
    Write32Le(Hdr + 88, EntriesCrc);
    HdrCrc = GptCrc32(Hdr, 92);
    Write32Le(Hdr + 16, HdrCrc);
    if (!BlockWriteSectors(1, 1, Hdr)) {
        DebugWrite("gpt: primary hdr fail\n");
        return 0;
    }

    /* Backup header */
    Write32Le(Hdr + 16, 0);
    Write64(Hdr + 24, BackupLba);
    Write64(Hdr + 32, 1);
    Write64(Hdr + 72, EntriesBackupLba);
    HdrCrc = GptCrc32(Hdr, 92);
    Write32Le(Hdr + 16, HdrCrc);
    if (!BlockWriteSectors((UINT32)BackupLba, 1, Hdr)) {
        DebugWrite("gpt: backup hdr fail\n");
        return 0;
    }

    if (OutEspLba) {
        *OutEspLba = EspLba;
    }
    if (OutEspSectors) {
        *OutEspSectors = EspSectors;
    }
    if (OutToyLba) {
        *OutToyLba = ToyLba;
    }
    if (OutToySectors) {
        *OutToySectors = ToySectors;
    }
    DebugWrite("gpt: wrote ESP@");
    DebugHex32(EspLba);
    DebugWrite(" TOYOS@");
    DebugHex32(ToyLba);
    DebugWrite("\n");
    return 1;
}
