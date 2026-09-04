/*
 * Gpt.c — MBR / GPT 分区表解析
 *
 * 读取 LBA0 判断启动方式，在分区表中查找 FAT12/16/32 类型分区。
 */
#include "Gpt.h"
#include "Block.h"
#include "Console.h"
#include "Debug.h"

#define SECTOR 512

static UINT8 gSector[SECTOR];

/* 检查扇区末尾 0x55AA 引导签名 */
static int SectorHasBootSig(void) {
    return gSector[510] == 0x55 && gSector[511] == 0xAA;
}

/* 判断 LBA0 是否为 FAT 引导扇区（superfloppy） */
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

/* 解析 MBR 分区表，返回 1=找到 FAT 分区并写出 LBA，2=GPT 保护分区，0=未找到 */
static int MbrFatLba(UINT32 *OutLba) {
    if (!SectorHasBootSig()) {
        return 0;
    }
    for (int i = 0; i < 4; i++) {
        UINT8 *P = gSector + 0x1BE + i * 16;
        UINT8 Type = P[4];
        if (Type == 0x00) {
            continue;
        }
        if (Type == 0x0B || Type == 0x0C || Type == 0x0E || Type == 0x06 || Type == 0x04) {
            UINT32 Start = (UINT32)P[8] | ((UINT32)P[9] << 8) |
                           ((UINT32)P[10] << 16) | ((UINT32)P[11] << 24);
            *OutLba = Start;
            return 1;
        }
        if (Type == 0xEE) {
            return 2;
        }
    }
    return 0;
}

/* 比较两个 16 字节 GUID 是否相等 */
static int GuidEq(const UINT8 *A, const UINT8 *B) {
    for (int i = 0; i < 16; i++) {
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

/* 解析 GPT 分区项，查找 EFI System / Basic Data；优先记 ESP 供只读挂载 */
static int GptFatLbaEx(UINT32 *OutLba, int *OutIsEsp) {
    UINT32 EspLba = 0;
    int HasEsp = 0;
    UINT32 DataLba = 0;
    int HasData = 0;

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
            if (GuidEq(Entry, EFI_PART)) {
                if (!HasEsp) {
                    EspLba = (UINT32)First;
                    HasEsp = 1;
                }
            } else if (GuidEq(Entry, BASIC_DATA)) {
                if (!HasData) {
                    DataLba = (UINT32)First;
                    HasData = 1;
                }
            }
        }
    }
    /* 有 Basic Data 优先作可写数据卷；仅 ESP 时仍可挂（只读） */
    if (HasData) {
        *OutLba = DataLba;
        if (OutIsEsp) {
            *OutIsEsp = 0;
        }
        return 1;
    }
    if (HasEsp) {
        *OutLba = EspLba;
        if (OutIsEsp) {
            *OutIsEsp = 1;
        }
        return 1;
    }
    return 0;
}

static int GptFatLba(UINT32 *OutLba) {
    return GptFatLbaEx(OutLba, 0);
}

/* 查找 FAT 卷起始 LBA（superfloppy / MBR / GPT 三选一） */
int GptFindFatStartEx(UINT32 *OutLba, int *OutIsEsp) {
    if (OutIsEsp) {
        *OutIsEsp = 0;
    }
    if (!OutLba) {
        return 0;
    }
    if (!BlockReadSectors(0, 1, gSector)) {
        return 0;
    }
    if (IsFatBootSector()) {
        *OutLba = 0;
        DebugWrite("FS: volume at LBA 0\n");
        return 1;
    }
    {
        int Mbr = MbrFatLba(OutLba);
        if (Mbr == 1) {
            DebugWrite("FS: MBR FAT partition LBA ");
            DebugHex32(*OutLba);
            DebugWrite("\n");
            return 1;
        }
        if (Mbr == 2 && GptFatLbaEx(OutLba, OutIsEsp)) {
            DebugWrite("FS: GPT FAT partition LBA ");
            DebugHex32(*OutLba);
            if (OutIsEsp && *OutIsEsp) {
                DebugWrite(" (ESP)\n");
            } else {
                DebugWrite("\n");
            }
            return 1;
        }
    }
    DebugWrite("FS: no FAT partition found\n");
    return 0;
}

int GptFindFatStart(UINT32 *OutLba) {
    return GptFindFatStartEx(OutLba, 0);
}
