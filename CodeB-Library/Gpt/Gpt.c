/*
 * Gpt.c — 查找 FAT 分区（PR-S-gpt-1）
 * 写盘布局：GptWrite.c
 */
#include "GptPrivate.h"
#include "Block.h"
#include "Debug.h"

UINT8 gGptSector[SECTOR];

/* 检查扇区末尾 0x55AA 引导签名 */
static int SectorHasBootSig(void) {
    return gGptSector[510] == 0x55 && gGptSector[511] == 0xAA;
}

/* 判断缓冲区是否为 FAT 引导扇区（superfloppy / 分区起点） */
static int IsFatBootSector(void) {
    if (!SectorHasBootSig()) {
        return 0;
    }
    if (gGptSector[0] != 0xEB && gGptSector[0] != 0xE9) {
        return 0;
    }
    if (gGptSector[82] == 'F' && gGptSector[83] == 'A' && gGptSector[84] == 'T') {
        return 1;
    }
    if (gGptSector[54] == 'F' && gGptSector[55] == 'A' && gGptSector[56] == 'T') {
        return 1;
    }
    return 0;
}

static int LbaLooksLikeFat(UINT32 Lba) {
    if (!BlockReadSectors(Lba, 1, gGptSector)) {
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
        UINT8 *P = gGptSector + 0x1BE + i * 16;
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

/*
 * 枚举 GPT：EFI System + Microsoft Basic Data；
 * 其它 GUID 若首扇区像 FAT 也收录（避免类型标错漏挂 TOYOS）。
 */
static int GptCollectFat(GPT_FAT_PART *Out, int Max, int *InOutCount) {
    if (!BlockReadSectors(1, 1, gGptSector)) {
        return 0;
    }
    if (*(UINT64 *)(void *)gGptSector != 0x5452415020494645ULL) {
        return 0;
    }
    {
        UINT32 EntriesLba = *(UINT32 *)(void *)(gGptSector + 72);
        UINT32 EntryCount = *(UINT32 *)(void *)(gGptSector + 80);
        UINT32 EntrySize = *(UINT32 *)(void *)(gGptSector + 84);
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
            if (!BlockReadSectors(Sec, 1, gGptSector)) {
                return 0;
            }
            for (j = 0; j < 128; j++) {
                Entry[j] = gGptSector[Off + j];
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
    if (!BlockReadSectors(0, 1, gGptSector)) {
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
