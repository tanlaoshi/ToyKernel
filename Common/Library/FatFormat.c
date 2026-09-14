/*
 * FatFormat.c — PR-FS-inst-1：简易 FAT32 格式化
 */
#include "Fat.h"
#include "FatPriv.h"
#include "Block.h"
#include "Debug.h"

static void ZeroBuf(UINT8 *P, UINTN N) {
    UINTN i;
    for (i = 0; i < N; i++) {
        P[i] = 0;
    }
}

static void PutLabel(UINT8 *Dst11, const char *Label) {
    int i;
    for (i = 0; i < 11; i++) {
        Dst11[i] = ' ';
    }
    if (!Label) {
        return;
    }
    for (i = 0; i < 11 && Label[i]; i++) {
        char C = Label[i];
        if (C >= 'a' && C <= 'z') {
            C = (char)(C - 'a' + 'A');
        }
        Dst11[i] = (UINT8)C;
    }
}

static int WriteZeroSectors(UINT32 Lba, UINT32 Count) {
    UINT32 i;
    ZeroBuf(gSector, SECTOR);
    for (i = 0; i < Count; i++) {
        if (!BlockWriteSectors(Lba + i, 1, gSector)) {
            return 0;
        }
    }
    return 1;
}

int FatFormatFat32(UINT32 StartLba, UINT32 SectorCount, const char *Label) {
    UINT32 Spc = 8;
    UINT32 Reserved = 32;
    UINT32 NumFats = 2;
    UINT32 FatSz = 1;
    UINT32 Clusters;
    UINT32 DataSec;
    UINT32 i;
    UINT32 FatLba;
    UINT32 DataLba;

    if (SectorCount < 0x10000u) {
        /* <32MiB：用较小簇 */
        Spc = 1;
    }
    if (SectorCount < Reserved + 64) {
        return FAT_ERR_INVAL;
    }

    for (;;) {
        DataSec = SectorCount - Reserved - NumFats * FatSz;
        Clusters = DataSec / Spc;
        {
            UINT32 Need = (UINT32)(((Clusters + 2ull) * 4ull + 511ull) / 512ull);
            if (Need <= FatSz) {
                break;
            }
            FatSz = Need;
            if (Reserved + NumFats * FatSz >= SectorCount) {
                return FAT_ERR_NOSPC;
            }
        }
    }
    if (Clusters < 65525u) {
        /* 仍标 FAT32；课堂分区通常够大 */
    }

    FatLba = StartLba + Reserved;
    DataLba = FatLba + NumFats * FatSz;

    /* BPB / VBR */
    ZeroBuf(gSector, SECTOR);
    gSector[0] = 0xEB;
    gSector[1] = 0x58;
    gSector[2] = 0x90;
    gSector[3] = 'T';
    gSector[4] = 'O';
    gSector[5] = 'Y';
    gSector[6] = 'O';
    gSector[7] = 'S';
    gSector[8] = ' ';
    gSector[9] = ' ';
    gSector[10] = ' ';
    Write16(gSector + 11, 512);
    gSector[13] = (UINT8)Spc;
    Write16(gSector + 14, (UINT16)Reserved);
    gSector[16] = (UINT8)NumFats;
    Write16(gSector + 17, 0);
    Write16(gSector + 19, 0);
    gSector[21] = 0xF8;
    Write16(gSector + 22, 0);
    Write16(gSector + 24, 63);
    Write16(gSector + 26, 255);
    Write32(gSector + 28, StartLba);
    Write32(gSector + 32, SectorCount);
    Write32(gSector + 36, FatSz);
    Write16(gSector + 40, 0);
    Write16(gSector + 42, 0);
    Write32(gSector + 44, 2); /* root cluster */
    Write16(gSector + 48, 1); /* FSInfo */
    Write16(gSector + 50, 6); /* backup boot */
    gSector[66] = 0x80;
    gSector[67] = 0x00;
    gSector[68] = 0x29;
    Write32(gSector + 69, 0x544F594F); /* serial 'TOYO' */
    PutLabel(gSector + 71, Label);
    gSector[82] = 'F';
    gSector[83] = 'A';
    gSector[84] = 'T';
    gSector[85] = '3';
    gSector[86] = '2';
    gSector[87] = ' ';
    gSector[88] = ' ';
    gSector[89] = ' ';
    gSector[510] = 0x55;
    gSector[511] = 0xAA;
    if (!BlockWriteSectors(StartLba, 1, gSector)) {
        return FAT_ERR_IO;
    }
    if (!BlockWriteSectors(StartLba + 6, 1, gSector)) {
        return FAT_ERR_IO;
    }

    /* FSInfo */
    ZeroBuf(gSector, SECTOR);
    Write32(gSector + 0, 0x41615252u);
    Write32(gSector + 484, 0x61417272u);
    Write32(gSector + 488, Clusters - 1);
    Write32(gSector + 492, 3);
    gSector[510] = 0x55;
    gSector[511] = 0xAA;
    if (!BlockWriteSectors(StartLba + 1, 1, gSector)) {
        return FAT_ERR_IO;
    }
    if (!BlockWriteSectors(StartLba + 7, 1, gSector)) {
        return FAT_ERR_IO;
    }

    /* FATs */
    for (i = 0; i < NumFats; i++) {
        UINT32 Base = FatLba + i * FatSz;
        if (!WriteZeroSectors(Base, FatSz)) {
            return FAT_ERR_IO;
        }
        ZeroBuf(gSector, SECTOR);
        /* cluster 0 media, 1 EOC, 2 root EOC */
        Write32(gSector + 0, 0x0FFFFFF8u);
        Write32(gSector + 4, 0x0FFFFFFFu);
        Write32(gSector + 8, 0x0FFFFFF8u);
        if (!BlockWriteSectors(Base, 1, gSector)) {
            return FAT_ERR_IO;
        }
    }

    /* Zero root cluster */
    if (!WriteZeroSectors(DataLba, Spc)) {
        return FAT_ERR_IO;
    }

    DebugWrite("fat: format FAT32 @");
    DebugHex32(StartLba);
    DebugWrite(" sectors=");
    DebugHex32(SectorCount);
    DebugWrite("\n");
    return FAT_OK;
}
