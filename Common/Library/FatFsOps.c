/*
 * FatFsOps.c — FAT 作为 VFS 第一后端（PR-F1 / PR-F2）
 */
#include "Vfs.h"
#include "Fat.h"

static const FS_OPS gFatFsOps = {
    .Name = "fat",
    .Mount = FatInit,
    .ListDir = FatListDir,
    .ListEntries = FatListEntries,
    .ReadFile = FatReadFile,
    .WriteFile = FatWriteFile,
    .DeleteFile = FatDeleteFile,
    .Mkdir = FatMkdir,
    .Rmdir = FatRmdir,
    .Rename = FatRename,
    .FileStat = FatFileStat,
    .FileSync = FatFileSync,
};

const FS_OPS *FatFsOps(void) {
    return &gFatFsOps;
}
