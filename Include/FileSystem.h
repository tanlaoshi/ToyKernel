/*
 * FileSystem.h — 文件系统模块（PR-FS2 多卷 / 路径前缀）
 *
 * 路径：`TOYOS:HELLO.ELF`、`A:DOCS`、`B:/x`；无前缀用默认卷（优先 TOYOS）。
 * Shell/exec/open/Theme 请走 Fs*；底层 Fat* 仍相对「当前已激活卷」。
 */
#ifndef FILE_SYSTEM_H
#define FILE_SYSTEM_H

#include "BootTypes.h"
#include "Fat.h"

#define FS_MAX_VOLUMES 4
#define FS_VOL_NAME_MAX 8

int FileSystemInit(void);

/* 卷数；下标 0..Count-1 */
int FileSystemVolCount(void);
int FileSystemDefaultVol(void);
/* Name 如 "TOYOS"/"A"；Flags 可选输出只读等，可为 NULL */
int FileSystemVolInfo(int Idx, char *Name, int NameMax, UINT32 *Drive,
                      UINT32 *StartLba, int *ReadOnly);

/* 解析 Path → 卷下标 + 卷内相对路径（可指向 Path 内子串） */
int FileSystemResolve(const char *Path, int *OutVol, const char **OutRel);

/* 切换 Block + FatInit 到指定卷；成功 FAT_OK */
int FileSystemActivate(int VolIdx);

/* 带前缀的路径操作（内部 Resolve+Activate+Fat*；写操作拒只读卷） */
int FsListDir(const char *Path);
/* PR-FB1：结构化目录枚举（供文件浏览器） */
int FsListEntries(const char *Path, FAT_DIR_ENT *Out, int Max, int *OutCount);
int FsReadFile(const char *Path, void *Buffer, UINTN MaxSize, UINTN *OutSize);
int FsWriteFile(const char *Path, const void *Buffer, UINTN Size);
int FsDeleteFile(const char *Path);
int FsMkdir(const char *Path);
int FsRmdir(const char *Path);
int FsRename(const char *OldPath, const char *NewPath);

#endif
