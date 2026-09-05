/*
 * Vfs.h — VFS 面 FsOps（PR-F1）
 *
 * 卷前缀 / 只读策略仍在 FileSystem；本头是「已激活卷」上的可插拔后端。
 * FAT 为第一个后端；Common 业务继续走 Fs*（内部经本表）。
 */
#ifndef VFS_H
#define VFS_H

#include "BootTypes.h"
#include "Fat.h"

typedef struct FS_OPS {
    const char *Name;
    /* 挂载：对已选中的 Block 设备，以 StartLba 为卷起点初始化后端 */
    int (*Mount)(UINT32 StartLba);
    int (*ListDir)(const char *Path);
    int (*ListEntries)(const char *Path, FAT_DIR_ENT *Out, int Max, int *OutCount);
    int (*ReadFile)(const char *Path, void *Buffer, UINTN MaxSize, UINTN *OutSize);
    int (*WriteFile)(const char *Path, const void *Buffer, UINTN Size);
    int (*DeleteFile)(const char *Path);
    int (*Mkdir)(const char *Path);
    int (*Rmdir)(const char *Path);
    int (*Rename)(const char *OldPath, const char *NewPath);
} FS_OPS;

/* 注册当前后端（F1：全局一份；F3 可扩多后端） */
int VfsRegister(const FS_OPS *Ops);
const FS_OPS *VfsOps(void);

/* 直接经表调用（已 Activate 的相对路径）；无后端 → FAT_ERR_IO */
int VfsMount(UINT32 StartLba);
int VfsListDir(const char *Path);
int VfsListEntries(const char *Path, FAT_DIR_ENT *Out, int Max, int *OutCount);
int VfsReadFile(const char *Path, void *Buffer, UINTN MaxSize, UINTN *OutSize);
int VfsWriteFile(const char *Path, const void *Buffer, UINTN Size);
int VfsDeleteFile(const char *Path);
int VfsMkdir(const char *Path);
int VfsRmdir(const char *Path);
int VfsRename(const char *OldPath, const char *NewPath);

/* FAT 后端描述符（定义于 FatFsOps.c） */
const FS_OPS *FatFsOps(void);

#endif
