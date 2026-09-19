/*
 * FileSystemOps.c — 路径上的读写与目录操作（PR-S-filesystem-1）
 */
#include "FileSystemPrivate.h"
#include "Store.h"
#include "ShellCommands.h"
#include "Block.h"
#include "Gpt.h"
#include "Vfs.h"
#include "Debug.h"
#include "ToySerialLog.h"
#include "Hal.h"
#include "CoreOps.h"

static int FileSystemPreparePath(const char *Path, const char **RelOut, int NeedWrite) {
    int Vol;
    const char *Rel;
    int Err;

    Err = FileSystemResolve(Path, &Vol, &Rel);
    if (Err != FAT_OK) {
        return Err;
    }
    if (NeedWrite && gVols[Vol].ReadOnly) {
        return FAT_ERR_ROFS;
    }
    Err = FileSystemActivate(Vol);
    if (Err != FAT_OK) {
        return Err;
    }
    if (RelOut) {
        *RelOut = Rel;
    }
    return FAT_OK;
}

int FileSystemListDirectory(const char *Path) {
    const char *Rel;
    int Err = FileSystemPreparePath(Path ? Path : "", &Rel, 0);
    if (Err != FAT_OK) {
        return Err;
    }
    if (!Path || Path[0] == 0) {
        Rel = 0;
    }
    return VfsListDir(Rel && Rel[0] ? Rel : 0);
}

int FileSystemListEntries(const char *Path, FAT_DIRECTORY_ENTRY *Out, int Max, int *OutCount) {
    const char *Rel;
    int Err = FileSystemPreparePath(Path ? Path : "", &Rel, 0);
    if (Err != FAT_OK) {
        return Err;
    }
    if (!Path || Path[0] == 0) {
        Rel = 0;
    }
    return VfsListEntries(Rel && Rel[0] ? Rel : 0, Out, Max, OutCount);
}

int FileSystemReadFile(const char *Path, void *Buffer, UINTN MaxSize, UINTN *OutSize) {
    const char *Rel;
    int Err;

    if (!Path) {
        return FAT_ERR_INVAL;
    }
    Err = FileSystemPreparePath(Path, &Rel, 0);
    if (Err != FAT_OK) {
        return Err;
    }
    return VfsReadFile(Rel, Buffer, MaxSize, OutSize);
}

int FileSystemWriteFile(const char *Path, const void *Buffer, UINTN Size) {
    const char *Rel;
    int Err = FileSystemPreparePath(Path, &Rel, 1);
    if (Err != FAT_OK) {
        return Err;
    }
    return VfsWriteFile(Rel, Buffer, Size);
}

int FileSystemReadFileAt(const char *Path, UINTN Offset, void *Buffer, UINTN Len, UINTN *OutN) {
    const char *Rel;
    int Err;

    if (!Path) {
        return FAT_ERR_INVAL;
    }
    Err = FileSystemPreparePath(Path, &Rel, 0);
    if (Err != FAT_OK) {
        return Err;
    }
    return VfsReadFileAt(Rel, Offset, Buffer, Len, OutN);
}

int FileSystemWriteFileAt(const char *Path, UINTN Offset, const void *Buffer, UINTN Len, UINTN *OutN) {
    const char *Rel;
    int Err = FileSystemPreparePath(Path, &Rel, 1);
    if (Err != FAT_OK) {
        return Err;
    }
    return VfsWriteFileAt(Rel, Offset, Buffer, Len, OutN);
}

int FileSystemDeleteFile(const char *Path) {
    const char *Rel;
    int Err;

    if (!StorePayloadBypassActive() && StoreIsManagedPayload(Path)) {
        return FAT_ERR_STORE;
    }
    Err = FileSystemPreparePath(Path, &Rel, 1);
    if (Err != FAT_OK) {
        return Err;
    }
    return VfsDeleteFile(Rel);
}

int FileSystemMakeDirectory(const char *Path) {
    const char *Rel;
    int Err = FileSystemPreparePath(Path, &Rel, 1);
    if (Err != FAT_OK) {
        return Err;
    }
    return VfsMkdir(Rel);
}

int FileSystemRemoveDirectory(const char *Path) {
    const char *Rel;
    int Err = FileSystemPreparePath(Path, &Rel, 1);
    if (Err != FAT_OK) {
        return Err;
    }
    return VfsRmdir(Rel);
}

int FileSystemRename(const char *OldPath, const char *NewPath) {
    const char *OldRel;
    const char *NewRel;
    int OldVol;
    int NewVol;
    int Err;

    if (!OldPath || !NewPath) {
        return FAT_ERR_INVAL;
    }
    if (!StorePayloadBypassActive() &&
        (StoreIsManagedPayload(OldPath) || StoreIsManagedPayload(NewPath))) {
        return FAT_ERR_STORE;
    }
    Err = FileSystemResolve(OldPath, &OldVol, &OldRel);
    if (Err != FAT_OK) {
        return Err;
    }
    Err = FileSystemResolve(NewPath, &NewVol, &NewRel);
    if (Err != FAT_OK) {
        return Err;
    }
    if (OldVol != NewVol) {
        return FAT_ERR_INVAL;
    }
    Err = FileSystemPreparePath(OldPath, &OldRel, 1);
    if (Err != FAT_OK) {
        return Err;
    }
    /* 同卷已 Activate；NewRel 相对路径 */
    Err = FileSystemResolve(NewPath, &NewVol, &NewRel);
    if (Err != FAT_OK) {
        return Err;
    }
    return VfsRename(OldRel, NewRel);
}

int FileSystemFileStat(const char *Path, FAT_FILE_STAT *Out) {
    const char *Rel;
    int Err = FileSystemPreparePath(Path ? Path : "", &Rel, 0);
    if (Err != FAT_OK) {
        return Err;
    }
    if (!Path || Path[0] == 0) {
        Rel = 0;
    }
    return VfsFileStat(Rel && Rel[0] ? Rel : 0, Out);
}

int FileSystemFileSync(const char *Path) {
    const char *Rel;
    int Err = FileSystemPreparePath(Path ? Path : "", &Rel, 0);
    if (Err != FAT_OK) {
        return Err;
    }
    (void)Rel;
    return VfsFileSync();
}

int FileSystemDirStress(const char *Path, int MaxFiles, int *OutCreated, int *OutGrew) {
    const char *Rel;
    const FS_OPS *Ops;
    int Err;

    if (!Path || !Path[0]) {
        return FAT_ERR_INVAL;
    }
    Err = FileSystemPreparePath(Path, &Rel, 1);
    if (Err != FAT_OK) {
        return Err;
    }
    Ops = VfsOps();
    if (!Ops || Ops->Synthetic) {
        return FAT_ERR_ROFS;
    }
    return FatDirStress(Rel && Rel[0] ? Rel : Path, MaxFiles, OutCreated, OutGrew);
}
