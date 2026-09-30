/*
 * FsStub.c — Host 契约测用 Synthetic 后端（单文件 HI.TXT）。
 */
#include "Vfs.h"

#include <string.h>

#define STUB_CAP 256

static char gData[STUB_CAP];
static UINTN gLen;
static int gMounted;

void FsStubReset(void)
{
    gLen = 0;
    gMounted = 0;
    gData[0] = 0;
}

static int StubMount(UINT32 StartLba)
{
    (void)StartLba;
    gMounted = 1;
    return FAT_OK;
}

static int StubReadFile(const char *Path, void *Buffer, UINTN MaxSize, UINTN *OutSize)
{
    UINTN N;

    if (!gMounted || !Path || !Buffer || !OutSize) {
        return FAT_ERR_INVAL;
    }
    if (strcmp(Path, "HI.TXT") != 0 && strcmp(Path, "/HI.TXT") != 0) {
        return FAT_ERR_NOENT;
    }
    N = gLen;
    if (N > MaxSize) {
        N = MaxSize;
    }
    memcpy(Buffer, gData, (size_t)N);
    *OutSize = N;
    return FAT_OK;
}

static int StubWriteFile(const char *Path, const void *Buffer, UINTN Size)
{
    if (!gMounted || !Path || !Buffer) {
        return FAT_ERR_INVAL;
    }
    if (strcmp(Path, "HI.TXT") != 0 && strcmp(Path, "/HI.TXT") != 0) {
        return FAT_ERR_NOENT;
    }
    if (Size > STUB_CAP) {
        return FAT_ERR_NOSPC;
    }
    memcpy(gData, Buffer, (size_t)Size);
    gLen = Size;
    return FAT_OK;
}

static int StubListDir(const char *Path)
{
    (void)Path;
    return gMounted ? FAT_OK : FAT_ERR_IO;
}

static const FS_OPS gStubOps = {
    .Name = "stub",
    .Mount = StubMount,
    .ListDir = StubListDir,
    .ListEntries = 0,
    .ReadFile = StubReadFile,
    .WriteFile = StubWriteFile,
    .DeleteFile = 0,
    .Mkdir = 0,
    .Rmdir = 0,
    .Rename = 0,
    .FileStat = 0,
    .FileSync = 0,
    .Synthetic = 1,
    .ReadFileAt = 0,
    .WriteFileAt = 0,
};

const FS_OPS *FsStubOps(void)
{
    return &gStubOps;
}
