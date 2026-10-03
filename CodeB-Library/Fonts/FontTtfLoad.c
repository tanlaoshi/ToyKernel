/*
 * FontTtfLoad.c — PR-UI-ttf-0：读 Assets/Fonts/CJK.TTF，校验 sfnt 头。
 * 不栅格、不挂钩 FontGlyphCp；缺文件 / 坏魔数软退。
 */
#include "Font.h"
#include "FileSystem.h"
#include "Fat.h"
#include "ToySerialLog.h"

#define TTF_PATH "Assets/Fonts/CJK.TTF"

static UINT16 Be16(const UINT8 *P) {
    return (UINT16)((UINT16)P[0] << 8) | (UINT16)P[1];
}

static UINT32 Be32(const UINT8 *P) {
    return ((UINT32)P[0] << 24) | ((UINT32)P[1] << 16) | ((UINT32)P[2] << 8) |
           (UINT32)P[3];
}

static int SfntOk(UINT32 Mag) {
    if (Mag == 0x00010000u) {
        return 1;
    }
    if (Mag == 0x74727565u) { /* 'true' */
        return 1;
    }
    if (Mag == 0x4F54544Fu) { /* 'OTTO' */
        return 1;
    }
    return 0;
}

int FontTtfLoad(void) {
    FAT_FILE_STAT St;
    UINT8 Hdr[12];
    UINTN Got;
    UINT32 Mag;
    UINT16 Tables;
    int Err;

    Got = 0;
    Err = FileSystemFileStat(TTF_PATH, &St);
    if (Err != FAT_OK || (St.Attr & FAT_ATTR_DIR) || St.Size < 12u) {
        ToyLogBoot("Boot: ttf miss\n");
        return -1;
    }
    Err = FileSystemReadFileAt(TTF_PATH, 0, Hdr, 12, &Got);
    if (Err != FAT_OK || Got < 12) {
        ToyLogBoot("Boot: ttf miss\n");
        return -1;
    }
    Mag = Be32(Hdr);
    if (!SfntOk(Mag)) {
        ToyLogBoot("Boot: ttf not sfnt mag=");
        ToyLogBootHex32(Mag);
        ToyLogBoot("\n");
        return -1;
    }
    Tables = Be16(Hdr + 4);
    ToyLogBoot("Boot: ttf sfnt ok bytes=");
    ToyLogBootHex32(St.Size);
    ToyLogBoot(" tables=");
    ToyLogBootHex32((UINT32)Tables);
    ToyLogBoot("\n");
    return 0;
}
