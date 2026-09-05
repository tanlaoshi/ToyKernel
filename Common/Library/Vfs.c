/*
 * Vfs.c — FsOps 注册与分发（PR-F1）
 */
#include "Vfs.h"

static const FS_OPS *gOps;

int VfsRegister(const FS_OPS *Ops) {
    if (!Ops || !Ops->Mount || !Ops->ReadFile || !Ops->WriteFile) {
        return -1;
    }
    gOps = Ops;
    return 0;
}

const FS_OPS *VfsOps(void) {
    return gOps;
}

static int NoBackend(void) {
    return FAT_ERR_IO;
}

int VfsMount(UINT32 StartLba) {
    if (!gOps || !gOps->Mount) {
        return NoBackend();
    }
    return gOps->Mount(StartLba);
}

int VfsListDir(const char *Path) {
    if (!gOps || !gOps->ListDir) {
        return NoBackend();
    }
    return gOps->ListDir(Path);
}

int VfsListEntries(const char *Path, FAT_DIR_ENT *Out, int Max, int *OutCount) {
    if (!gOps || !gOps->ListEntries) {
        return NoBackend();
    }
    return gOps->ListEntries(Path, Out, Max, OutCount);
}

int VfsReadFile(const char *Path, void *Buffer, UINTN MaxSize, UINTN *OutSize) {
    if (!gOps || !gOps->ReadFile) {
        return NoBackend();
    }
    return gOps->ReadFile(Path, Buffer, MaxSize, OutSize);
}

int VfsWriteFile(const char *Path, const void *Buffer, UINTN Size) {
    if (!gOps || !gOps->WriteFile) {
        return NoBackend();
    }
    return gOps->WriteFile(Path, Buffer, Size);
}

int VfsDeleteFile(const char *Path) {
    if (!gOps || !gOps->DeleteFile) {
        return NoBackend();
    }
    return gOps->DeleteFile(Path);
}

int VfsMkdir(const char *Path) {
    if (!gOps || !gOps->Mkdir) {
        return NoBackend();
    }
    return gOps->Mkdir(Path);
}

int VfsRmdir(const char *Path) {
    if (!gOps || !gOps->Rmdir) {
        return NoBackend();
    }
    return gOps->Rmdir(Path);
}

int VfsRename(const char *OldPath, const char *NewPath) {
    if (!gOps || !gOps->Rename) {
        return NoBackend();
    }
    return gOps->Rename(OldPath, NewPath);
}
