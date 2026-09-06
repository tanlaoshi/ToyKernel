/*
 * CoreOps.h — PR-R4：Core 经 ops 表调 Services（不再直 #include Gui/FileSystem）
 *
 * WindowOps：用户窗协议（Syscall）
 * VfsServiceOps：多卷 FileSystem* 门面（Process / TaskFd）；与 Library VfsOps/FS_OPS 不同层
 */
#ifndef CORE_OPS_H
#define CORE_OPS_H

#include "BootTypes.h"
#include "Fat.h"

typedef struct {
    int (*OpenUser)(const char *Title, UINT32 W, UINT32 H);
    int (*DamageUser)(int Wid, const char *Text);
    int (*PollUserInput)(int Wid);
    int (*AddButton)(int Wid, int ButtonId, const char *Label);
} WINDOW_OPS;

void WindowOpsRegister(const WINDOW_OPS *Ops);
int WindowOpenUser(const char *Title, UINT32 W, UINT32 H);
int WindowDamageUser(int Wid, const char *Text);
int WindowPollUserInput(int Wid);
int WindowAddButton(int Wid, int ButtonId, const char *Label);

typedef struct {
    int (*ReadFile)(const char *Path, void *Buffer, UINTN MaxSize, UINTN *OutSize);
    int (*WriteFile)(const char *Path, const void *Buffer, UINTN Size);
    int (*ListEntries)(const char *Path, FAT_DIR_ENT *Out, int Max, int *OutCount);
    int (*FileStat)(const char *Path, FAT_FILE_STAT *Out);
} VFS_SERVICE_OPS;

void VfsServiceOpsRegister(const VFS_SERVICE_OPS *Ops);
int VfsServiceReadFile(const char *Path, void *Buffer, UINTN MaxSize, UINTN *OutSize);
int VfsServiceWriteFile(const char *Path, const void *Buffer, UINTN Size);
int VfsServiceListEntries(const char *Path, FAT_DIR_ENT *Out, int Max, int *OutCount);
int VfsServiceFileStat(const char *Path, FAT_FILE_STAT *Out);

#endif
