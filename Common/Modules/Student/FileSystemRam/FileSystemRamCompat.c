/*
 * FileSystemRamCompat.c — FS=ram 时顶替仍被框架引用的 Fat* 符号。
 * 真 FAT 实现在 Modules/FileSystemFat；本文件只让内核链得过。
 */
#include "Fat.h"

const char *FatStrError(int Err)
{
    (void)Err;
    return "ramfs";
}

int FatInit(UINT32 StartLba)
{
    (void)StartLba;
    return FAT_OK;
}

int FatWriteFile(const char *Path, const void *Buffer, UINTN Size)
{
    (void)Path;
    (void)Buffer;
    (void)Size;
    return FAT_ERR_ROFS;
}

int FatMkdir(const char *Path)
{
    (void)Path;
    return FAT_ERR_ROFS;
}

int FatFormatFat32(UINT32 StartLba, UINT32 SectorCount, const char *Label)
{
    (void)StartLba;
    (void)SectorCount;
    (void)Label;
    return FAT_ERR_ROFS;
}

void FatSetIoBreath(void (*Fn)(void))
{
    (void)Fn;
}

int FatDirStress(const char *DirPath, int MaxFiles, int *OutCreated, int *OutGrew)
{
    (void)DirPath;
    (void)MaxFiles;
    if (OutCreated) {
        *OutCreated = 0;
    }
    if (OutGrew) {
        *OutGrew = 0;
    }
    return FAT_ERR_ROFS;
}
