/*
 * ResFs.c — 只读「资源卷」第二 VFS 后端（PR-F3）
 *
 * 无 Block：内核内嵌只读文件表。挂载名 RES:；写/删/建目录一律 ROFS。
 * 另内嵌桌面 Assets（图标/壁纸）：真机无 USB TOYOS FAT 时 Desktop 可回退 RES:。
 * ListDir 经 LibWrite（PR-R4；ConsoleInit 注册 ConsoleWrite）。
 */
#include "Vfs.h"
#include "Fat.h"
#include "LibWrite.h"
#include "DesktopAssetsData.h"

typedef struct {
    const char *Name;
    const UINT8 *Data;
    UINT32 Size;
} RES_FILE;

static const UINT8 gReadme[] =
    "ToyOS resource volume (PR-F3)\n"
    "Read-only; includes embedded desktop Assets for real-PC fallback.\n";
static const UINT8 gHello[] = "hello from RES\n";
static const UINT8 gVersion[] = "resfs/2\n";

static const RES_FILE gFiles[] = {
    { "README.TXT", gReadme, (UINT32)(sizeof(gReadme) - 1) },
    { "HELLO.TXT", gHello, (UINT32)(sizeof(gHello) - 1) },
    { "VERSION.TXT", gVersion, (UINT32)(sizeof(gVersion) - 1) },
    { "Assets/Icons/bmp48/SHELL.BMP", gBmpShell, 0 },
    { "Assets/Icons/bmp48/SET.BMP", gBmpSet, 0 },
    { "Assets/Icons/bmp48/FILES.BMP", gBmpFiles, 0 },
    { "Assets/Icons/bmp48/START.BMP", gBmpStart, 0 },
    { "Assets/Images/WALL.BMP", gBmpWall, 0 },
};

#define RES_FILE_COUNT ((int)(sizeof(gFiles) / sizeof(gFiles[0])))

static UINT32 ResFileSize(const RES_FILE *F) {
    if (F->Size != 0) {
        return F->Size;
    }
    /* DesktopAssetsData：Size 在独立符号里 */
    if (F->Data == gBmpShell) {
        return gBmpShellSize;
    }
    if (F->Data == gBmpSet) {
        return gBmpSetSize;
    }
    if (F->Data == gBmpFiles) {
        return gBmpFilesSize;
    }
    if (F->Data == gBmpStart) {
        return gBmpStartSize;
    }
    if (F->Data == gBmpWall) {
        return gBmpWallSize;
    }
    return 0;
}

static int NameEq(const char *A, const char *B) {
    while (*A && *B) {
        char Ca = *A;
        char Cb = *B;
        if (Ca >= 'a' && Ca <= 'z') {
            Ca = (char)(Ca - 'a' + 'A');
        }
        if (Cb >= 'a' && Cb <= 'z') {
            Cb = (char)(Cb - 'a' + 'A');
        }
        if (Ca == '\\') {
            Ca = '/';
        }
        if (Cb == '\\') {
            Cb = '/';
        }
        if (Ca != Cb) {
            return 0;
        }
        A++;
        B++;
    }
    return *A == 0 && *B == 0;
}

static int IsRootPath(const char *Path) {
    return !Path || !Path[0] || (Path[0] == '/' && !Path[1]) ||
           (Path[0] == '\\' && !Path[1]);
}

static const RES_FILE *FindFile(const char *Path) {
    const char *P = Path;
    int i;

    if (IsRootPath(Path)) {
        return 0;
    }
    if (P[0] == '/' || P[0] == '\\') {
        P++;
    }
    for (i = 0; i < RES_FILE_COUNT; i++) {
        if (NameEq(P, gFiles[i].Name)) {
            return &gFiles[i];
        }
    }
    return 0;
}

static int ResMount(UINT32 StartLba) {
    (void)StartLba;
    return FAT_OK;
}

static int ResListDir(const char *Path) {
    int i;

    if (!IsRootPath(Path)) {
        return FAT_ERR_NOENT;
    }
    for (i = 0; i < RES_FILE_COUNT; i++) {
        LibWrite(gFiles[i].Name);
        LibWrite("\n");
    }
    return FAT_OK;
}

static int ResListEntries(const char *Path, FAT_DIRECTORY_ENTRY *Out, int Max, int *OutCount) {
    int i;
    int N;

    if (!Out || !OutCount || Max <= 0) {
        return FAT_ERR_INVAL;
    }
    if (!IsRootPath(Path)) {
        return FAT_ERR_NOENT;
    }
    N = RES_FILE_COUNT;
    if (N > Max) {
        N = Max;
    }
    for (i = 0; i < N; i++) {
        int j;
        for (j = 0; gFiles[i].Name[j] && j < FAT_ENT_NAME_MAX - 1; j++) {
            Out[i].Name[j] = gFiles[i].Name[j];
        }
        Out[i].Name[j] = 0;
        Out[i].Attr = FAT_ATTR_RO;
        Out[i].Size = ResFileSize(&gFiles[i]);
    }
    *OutCount = N;
    return FAT_OK;
}

static int ResReadFile(const char *Path, void *Buffer, UINTN MaxSize, UINTN *OutSize) {
    const RES_FILE *F;
    UINTN N;
    UINTN i;
    UINT8 *Dst;

    if (!Buffer) {
        return FAT_ERR_INVAL;
    }
    F = FindFile(Path);
    if (!F) {
        return FAT_ERR_NOENT;
    }
    N = ResFileSize(F);
    if (N > MaxSize) {
        N = MaxSize;
    }
    Dst = (UINT8 *)Buffer;
    for (i = 0; i < N; i++) {
        Dst[i] = F->Data[i];
    }
    if (OutSize) {
        *OutSize = N;
    }
    return FAT_OK;
}

static int ResWriteFile(const char *Path, const void *Buffer, UINTN Size) {
    (void)Path;
    (void)Buffer;
    (void)Size;
    return FAT_ERR_ROFS;
}

static int ResDeleteFile(const char *Path) {
    (void)Path;
    return FAT_ERR_ROFS;
}

static int ResMkdir(const char *Path) {
    (void)Path;
    return FAT_ERR_ROFS;
}

static int ResRmdir(const char *Path) {
    (void)Path;
    return FAT_ERR_ROFS;
}

static int ResRename(const char *OldPath, const char *NewPath) {
    (void)OldPath;
    (void)NewPath;
    return FAT_ERR_ROFS;
}

static int ResFileStat(const char *Path, FAT_FILE_STAT *Out) {
    const RES_FILE *F;

    if (!Out) {
        return FAT_ERR_INVAL;
    }
    if (IsRootPath(Path)) {
        Out->Attr = FAT_ATTR_DIR | FAT_ATTR_RO;
        Out->Size = 0;
        Out->Cluster = 0;
        return FAT_OK;
    }
    F = FindFile(Path);
    if (!F) {
        return FAT_ERR_NOENT;
    }
    Out->Attr = FAT_ATTR_RO;
    Out->Size = ResFileSize(F);
    Out->Cluster = 0;
    return FAT_OK;
}

static int ResFileSync(void) {
    return FAT_OK;
}

static const FS_OPS gResFsOps = {
    .Name = "res",
    .Mount = ResMount,
    .ListDir = ResListDir,
    .ListEntries = ResListEntries,
    .ReadFile = ResReadFile,
    .WriteFile = ResWriteFile,
    .DeleteFile = ResDeleteFile,
    .Mkdir = ResMkdir,
    .Rmdir = ResRmdir,
    .Rename = ResRename,
    .FileStat = ResFileStat,
    .FileSync = ResFileSync,
    .Synthetic = 1,
};

const FS_OPS *ResFsOps(void) {
    return &gResFsOps;
}
