/*
 * Fat.h — FAT16/FAT32 只读文件系统接口
 *
 * 仅支持根目录列举与按 8.3 短文件名读取文件，无子目录与 VFAT 长文件名。
 */
#ifndef FAT_H
#define FAT_H

#include "BootConfig.h"

int FatInit(UINT32 StartLba);
int FatListRoot(void);
int FatReadFile(const char *Path, void *Buffer, UINTN MaxSize, UINTN *OutSize);

#endif
