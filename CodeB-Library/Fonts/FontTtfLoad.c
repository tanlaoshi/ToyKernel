/*
 * FontTtfLoad.c — PR-UI-ttf-0/1：读 Assets/Fonts/CJK.TTF 进内存并校验 sfnt。
 * 不挂钩 FontGlyphCp。
 */
#include "Font.h"
#include "FileSystem.h"
#include "Fat.h"
#include "PhysicalMemory.h"
#include "ToySerialLog.h"

#define TTF_PATH "Assets/Fonts/CJK.TTF"
#define TTF_MAX  (8u * 1024u * 1024u)

static UINT8 *gBlob;
static UINT32 gPages;
static UINT32 gSize;
static int gOk;

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
    if (Mag == 0x74727565u) {
        return 1;
    }
    if (Mag == 0x4F54544Fu) {
        return 1;
    }
    return 0;
}

static void TtfFree(void) {
    if (gBlob && gPages) {
        PhysicalMemoryFreePages(gBlob, gPages);
    }
    gBlob = 0;
    gPages = 0;
    gSize = 0;
    gOk = 0;
}

const UINT8 *FontTtfBlob(UINT32 *OutSize) {
    if (OutSize) {
        *OutSize = gSize;
    }
    return gOk ? gBlob : 0;
}

int FontTtfLoad(void) {
    FAT_FILE_STAT St;
    UINTN Got;
    UINT32 Mag;
    UINT16 Tables;
    int Err;

    TtfFree();
    Got = 0;
    Err = FileSystemFileStat(TTF_PATH, &St);
    if (Err != FAT_OK || (St.Attr & FAT_ATTR_DIR) || St.Size < 12u) {
        ToyLogBoot("Boot: ttf miss\n");
        return -1;
    }
    if (St.Size > TTF_MAX) {
        ToyLogBoot("Boot: ttf too big\n");
        return -1;
    }
    gPages = (St.Size + 4095u) / 4096u;
    if (gPages == 0) {
        gPages = 1;
    }
    gBlob = (UINT8 *)PhysicalMemoryAllocatePages(gPages);
    if (!gBlob) {
        gPages = 0;
        ToyLogBoot("Boot: ttf oom\n");
        return -1;
    }
    Err = FileSystemReadFile(TTF_PATH, gBlob, St.Size, &Got);
    if (Err != FAT_OK || Got < 12) {
        TtfFree();
        ToyLogBoot("Boot: ttf miss\n");
        return -1;
    }
    Mag = Be32(gBlob);
    if (!SfntOk(Mag)) {
        TtfFree();
        ToyLogBoot("Boot: ttf not sfnt mag=");
        ToyLogBootHex32(Mag);
        ToyLogBoot("\n");
        return -1;
    }
    gSize = (UINT32)Got;
    gOk = 1;
    Tables = Be16(gBlob + 4);
    ToyLogBoot("Boot: ttf sfnt ok bytes=");
    ToyLogBootHex32(gSize);
    ToyLogBoot(" tables=");
    ToyLogBootHex32((UINT32)Tables);
    ToyLogBoot("\n");
    return 0;
}
