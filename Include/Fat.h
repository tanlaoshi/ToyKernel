/*
 * Fat.h — FAT16/FAT32 文件系统接口（PR-FS1）
 *
 * 依赖 Block 读写扇区。支持 8.3 与 LFN；路径含 . / ..；读/写/删/建目录。
 * 公开 API 统一：返回 0 (FAT_OK) 成功，负值错误码；细节见 FatStrError。
 */
#ifndef FAT_H
#define FAT_H

#include "BootTypes.h"

#define FAT_OK              0
#define FAT_ERR_IO         (-1)   /* 块设备读写失败 */
#define FAT_ERR_NOENT      (-2)   /* 路径不存在 */
#define FAT_ERR_NOSPC      (-3)   /* 无空闲簇或目录项 */
#define FAT_ERR_NOTDIR     (-4)   /* 路径组件不是目录 */
#define FAT_ERR_ISDIR      (-5)   /* 期望文件却是目录 */
#define FAT_ERR_NOTEMPTY   (-6)   /* 目录非空 */
#define FAT_ERR_EXIST      (-7)   /* 已存在 */
#define FAT_ERR_INVAL      (-8)   /* 非法参数/路径 */
#define FAT_ERR_NAMETOOLONG (-9)  /* 名过长 */
#define FAT_ERR_FBIG       (-10)  /* 超过写大小上限 */
#define FAT_ERR_ROFS       (-11)  /* 只读卷（PR-FS2 ESP 等） */

#define FAT_ATTR_DIR        0x10
#define FAT_ENT_NAME_MAX    64
#define FAT_LIST_MAX        64

typedef struct {
    char   Name[FAT_ENT_NAME_MAX];
    UINT8  Attr;
    UINT32 Size;
} FAT_DIR_ENT;

const char *FatStrError(int Err);

int FatInit(UINT32 StartLba);
int FatListRoot(void);
int FatListDir(const char *Path);
/* 枚举目录项到 Out[0..*OutCount)；成功 FAT_OK；Max<=0 或 Out 空 → INVAL */
int FatListEntries(const char *Path, FAT_DIR_ENT *Out, int Max, int *OutCount);
int FatReadFile(const char *Path, void *Buffer, UINTN MaxSize, UINTN *OutSize);
int FatWriteFile(const char *Path, const void *Buffer, UINTN Size);
int FatDeleteFile(const char *Path);
int FatMkdir(const char *Path);
int FatRmdir(const char *Path);

#endif
