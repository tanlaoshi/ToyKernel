/*
 * Fat.h — FAT16/FAT32 文件系统接口
 *
 * 依赖 Block 读写扇区。根目录 + 8.3 短文件名；支持读与覆盖/新建写。
 */
#ifndef FAT_H
#define FAT_H

#include "BootConfig.h"

int FatInit(UINT32 StartLba);
int FatListRoot(void);
int FatReadFile(const char *Path, void *Buffer, UINTN MaxSize, UINTN *OutSize);
/* 写入/覆盖根目录文件；成功返回 1 */
int FatWriteFile(const char *Path, const void *Buffer, UINTN Size);

#endif
