/*
 * CoreOps.c — PR-R4：WindowOps / VfsServiceOps 注册与薄分发
 */
#include "CoreOps.h"

static WINDOW_OPS gWindowOps;
static VFS_SERVICE_OPS gVfsServiceOps;

void WindowOpsRegister(const WINDOW_OPS *Ops) {
    if (!Ops) {
        gWindowOps.OpenUser = 0;
        gWindowOps.DamageUser = 0;
        gWindowOps.PollUserInput = 0;
        gWindowOps.AddButton = 0;
        return;
    }
    gWindowOps = *Ops;
}

int WindowOpenUser(const char *Title, UINT32 W, UINT32 H) {
    if (!gWindowOps.OpenUser) {
        return -1;
    }
    return gWindowOps.OpenUser(Title, W, H);
}

int WindowDamageUser(int Wid, const char *Text) {
    if (!gWindowOps.DamageUser) {
        return -1;
    }
    return gWindowOps.DamageUser(Wid, Text);
}

int WindowPollUserInput(int Wid) {
    if (!gWindowOps.PollUserInput) {
        return -1;
    }
    return gWindowOps.PollUserInput(Wid);
}

int WindowAddButton(int Wid, int ButtonId, const char *Label) {
    if (!gWindowOps.AddButton) {
        return -1;
    }
    return gWindowOps.AddButton(Wid, ButtonId, Label);
}

void VfsServiceOpsRegister(const VFS_SERVICE_OPS *Ops) {
    if (!Ops) {
        gVfsServiceOps.ReadFile = 0;
        gVfsServiceOps.WriteFile = 0;
        gVfsServiceOps.ListEntries = 0;
        gVfsServiceOps.FileStat = 0;
        return;
    }
    gVfsServiceOps = *Ops;
}

int VfsServiceReadFile(const char *Path, void *Buffer, UINTN MaxSize, UINTN *OutSize) {
    if (!gVfsServiceOps.ReadFile) {
        return FAT_ERR_IO;
    }
    return gVfsServiceOps.ReadFile(Path, Buffer, MaxSize, OutSize);
}

int VfsServiceWriteFile(const char *Path, const void *Buffer, UINTN Size) {
    if (!gVfsServiceOps.WriteFile) {
        return FAT_ERR_IO;
    }
    return gVfsServiceOps.WriteFile(Path, Buffer, Size);
}

int VfsServiceListEntries(const char *Path, FAT_DIR_ENT *Out, int Max, int *OutCount) {
    if (!gVfsServiceOps.ListEntries) {
        return FAT_ERR_IO;
    }
    return gVfsServiceOps.ListEntries(Path, Out, Max, OutCount);
}

int VfsServiceFileStat(const char *Path, FAT_FILE_STAT *Out) {
    if (!gVfsServiceOps.FileStat) {
        return FAT_ERR_IO;
    }
    return gVfsServiceOps.FileStat(Path, Out);
}
