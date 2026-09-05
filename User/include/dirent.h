/*
 * dirent.h — 用户态目录 / 文件状态（PR-F4）
 *
 * 名称写全：OpenDirectory / ReadDirectory / FileStat（勿用孤立 opendir/stat）。
 * 布局与内核 FAT_DIR_ENT / FAT_FILE_STAT 一致，经 syscall 拷贝。
 */
#ifndef DIRENT_H
#define DIRENT_H

#include <stddef.h>

#define TOY_ENT_NAME_MAX 64
#define TOY_ATTR_RO      0x01
#define TOY_ATTR_DIR     0x10
#define TOY_LIST_MAX     64

typedef struct {
    char          Name[TOY_ENT_NAME_MAX];
    unsigned char Attr;
    unsigned      Size;
} TOY_DIR_ENT;

typedef struct {
    unsigned char Attr;
    unsigned      Size;
    unsigned      Cluster;
} TOY_FILE_STAT;

typedef struct ToyDirectory TOY_DIR;

/* 打开目录（空路径 / "" = 默认卷根）；失败 NULL */
TOY_DIR *OpenDirectory(const char *path);
/* 读下一项：1=有项，0=结束，-1=失败 */
int ReadDirectory(TOY_DIR *dir, TOY_DIR_ENT *out);
/* 关闭目录句柄 */
int CloseDirectory(TOY_DIR *dir);
/* 查询路径状态；空路径 = 卷根。成功 0，失败 -1 */
int FileStat(const char *path, TOY_FILE_STAT *out);

#endif
