/*
 * FileSystemRam.c — 学生 FS 模板（Synthetic 内存盘）。
 *
 * 不碰 Block；挂载后在内存里读写若干文件。默认不链进内核。
 * 编进内核：make FS=ram（提供 FatFsOps 别名，挂载路径不用改）。
 * Host：./Scripts/runtests.sh fs
 */
#include "Vfs.h"

#define RAM_MAX_FILES 8
#define RAM_NAME_MAX  32
#define RAM_DATA_MAX  256

typedef struct {
    int Used;
    char Name[RAM_NAME_MAX];
    UINT8 Data[RAM_DATA_MAX];
    UINTN Len;
} RAM_FILE;

static RAM_FILE gFiles[RAM_MAX_FILES];
static int gMounted;

static void Zero(void *P, UINTN N)
{
    UINT8 *B = (UINT8 *)P;
    UINTN i;

    for (i = 0; i < N; i++) {
        B[i] = 0;
    }
}

static void Copy(void *Dst, const void *Src, UINTN N)
{
    UINT8 *D = (UINT8 *)Dst;
    const UINT8 *S = (const UINT8 *)Src;
    UINTN i;

    for (i = 0; i < N; i++) {
        D[i] = S[i];
    }
}

static int NameEq(const char *A, const char *B)
{
    while (*A && *B) {
        char Ca = *A;
        char Cb = *B;

        if (Ca >= 'a' && Ca <= 'z') {
            Ca = (char)(Ca - 'a' + 'A');
        }
        if (Cb >= 'a' && Cb <= 'z') {
            Cb = (char)(Cb - 'a' + 'A');
        }
        if (Ca != Cb) {
            return 0;
        }
        A++;
        B++;
    }
    return *A == 0 && *B == 0;
}

static void CopyName(char *Dst, const char *Src)
{
    int i;

    for (i = 0; Src[i] && i < RAM_NAME_MAX - 1; i++) {
        Dst[i] = Src[i];
    }
    Dst[i] = 0;
}

static const char *Leaf(const char *Path)
{
    const char *P = Path;

    if (!P) {
        return "";
    }
    if (P[0] == '/' || P[0] == '\\') {
        P++;
    }
    return P;
}

void RamFsReset(void)
{
    Zero(gFiles, sizeof(gFiles));
    gMounted = 0;
}

static int RamMount(UINT32 StartLba)
{
    (void)StartLba;
    gMounted = 1;
    return FAT_OK;
}

static RAM_FILE *Find(const char *Path)
{
    const char *Name = Leaf(Path);
    int i;

    if (!Name[0]) {
        return 0;
    }
    for (i = 0; i < RAM_MAX_FILES; i++) {
        if (gFiles[i].Used && NameEq(gFiles[i].Name, Name)) {
            return &gFiles[i];
        }
    }
    return 0;
}

static RAM_FILE *AllocSlot(const char *Path)
{
    const char *Name = Leaf(Path);
    int i;

    if (!Name[0]) {
        return 0;
    }
    for (i = 0; i < RAM_MAX_FILES; i++) {
        if (!gFiles[i].Used) {
            gFiles[i].Used = 1;
            CopyName(gFiles[i].Name, Name);
            gFiles[i].Len = 0;
            return &gFiles[i];
        }
    }
    return 0;
}

static int RamReadFile(const char *Path, void *Buffer, UINTN MaxSize, UINTN *OutSize)
{
    RAM_FILE *F;
    UINTN N;

    if (!gMounted || !Path || !Buffer || !OutSize) {
        return FAT_ERR_INVAL;
    }
    F = Find(Path);
    if (!F) {
        return FAT_ERR_NOENT;
    }
    N = F->Len;
    if (N > MaxSize) {
        N = MaxSize;
    }
    Copy(Buffer, F->Data, N);
    *OutSize = N;
    return FAT_OK;
}

static int RamWriteFile(const char *Path, const void *Buffer, UINTN Size)
{
    RAM_FILE *F;

    if (!gMounted || !Path || !Buffer) {
        return FAT_ERR_INVAL;
    }
    if (Size > RAM_DATA_MAX) {
        return FAT_ERR_NOSPC;
    }
    F = Find(Path);
    if (!F) {
        F = AllocSlot(Path);
        if (!F) {
            return FAT_ERR_NOSPC;
        }
    }
    Copy(F->Data, Buffer, Size);
    F->Len = Size;
    return FAT_OK;
}

static int RamDeleteFile(const char *Path)
{
    RAM_FILE *F;

    if (!gMounted || !Path) {
        return FAT_ERR_INVAL;
    }
    F = Find(Path);
    if (!F) {
        return FAT_ERR_NOENT;
    }
    Zero(F, sizeof(*F));
    return FAT_OK;
}

static int RamListDir(const char *Path)
{
    (void)Path;
    return gMounted ? FAT_OK : FAT_ERR_IO;
}

static int RamListEntries(const char *Path, FAT_DIRECTORY_ENTRY *Out, int Max,
                          int *OutCount)
{
    int i;
    int N = 0;

    (void)Path;
    if (!gMounted || !Out || !OutCount || Max <= 0) {
        return FAT_ERR_INVAL;
    }
    for (i = 0; i < RAM_MAX_FILES && N < Max; i++) {
        if (!gFiles[i].Used) {
            continue;
        }
        CopyName(Out[N].Name, gFiles[i].Name);
        Out[N].Attr = 0;
        Out[N].Size = (UINT32)gFiles[i].Len;
        N++;
    }
    *OutCount = N;
    return FAT_OK;
}

static int RamFileStat(const char *Path, FAT_FILE_STAT *Out)
{
    RAM_FILE *F;

    if (!gMounted || !Path || !Out) {
        return FAT_ERR_INVAL;
    }
    F = Find(Path);
    if (!F) {
        return FAT_ERR_NOENT;
    }
    Zero(Out, sizeof(*Out));
    Out->Size = (UINT32)F->Len;
    Out->Attr = 0;
    return FAT_OK;
}

static int RamFileSync(void)
{
    return gMounted ? FAT_OK : FAT_ERR_IO;
}

static int RamMkdir(const char *Path)
{
    (void)Path;
    return FAT_ERR_ROFS;
}

static int RamRmdir(const char *Path)
{
    (void)Path;
    return FAT_ERR_ROFS;
}

static int RamRename(const char *OldPath, const char *NewPath)
{
    (void)OldPath;
    (void)NewPath;
    return FAT_ERR_ROFS;
}

static const FS_OPS gRamOps = {
    .Name = "ram",
    .Mount = RamMount,
    .ListDir = RamListDir,
    .ListEntries = RamListEntries,
    .ReadFile = RamReadFile,
    .WriteFile = RamWriteFile,
    .DeleteFile = RamDeleteFile,
    .Mkdir = RamMkdir,
    .Rmdir = RamRmdir,
    .Rename = RamRename,
    .FileStat = RamFileStat,
    .FileSync = RamFileSync,
    .Synthetic = 1,
    .ReadFileAt = 0,
    .WriteFileAt = 0,
};

const FS_OPS *RamFsOps(void)
{
    return &gRamOps;
}

/* FS=ram 时顶替 FatFsOps，FileSystem* 调用点不用改 */
const FS_OPS *FatFsOps(void)
{
    return &gRamOps;
}
