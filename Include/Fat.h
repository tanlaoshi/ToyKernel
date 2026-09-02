/*
 * Fat.h — FAT16/FAT32 文件系统接口
 *
 * 依赖 Block 读写扇区。支持 8.3 与 LFN；路径 DIR/FILE、读/写/删。
 */
#ifndef FAT_H
#define FAT_H

#include "BootTypes.h"

int FatInit(UINT32 StartLba);
int FatListRoot(void);
int FatListDir(const char *Path);
int FatReadFile(const char *Path, void *Buffer, UINTN MaxSize, UINTN *OutSize);
int FatWriteFile(const char *Path, const void *Buffer, UINTN Size);
int FatDeleteFile(const char *Path);

#endif
