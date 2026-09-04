/*
 * FileSystem.c — Block + GPT + FAT；PR-FS2 多卷与路径前缀
 */
#include "FileSystem.h"
#include "Block.h"
#include "Gpt.h"
#include "Fat.h"
#include "Console.h"
#include "Debug.h"
#include "Hal.h"

typedef struct {
    UINT32 Drive;
    UINT32 StartLba;
    char   Name[FS_VOL_NAME_MAX]; /* 前缀名，不含冒号：TOYOS / A / ESP */
    char   Letter;                /* 'A'+index，便于 A: 访问 */
    int    ReadOnly;
    int    HasToyId;
} FS_VOLUME;

static FS_VOLUME gVols[FS_MAX_VOLUMES];
static int gVolCount;
static int gDefaultVol;
static int gActiveVol = -1;
static UINT32 gActiveDrive = 0xFFFFFFFFu;
static UINT32 gActiveLba = 0xFFFFFFFFu;

static void FatReport(const char *Cmd, int Err) {
    ConsoleWrite(Cmd);
    ConsoleWrite(": ");
    ConsoleWrite(FatStrError(Err));
    ConsoleWrite("\n");
}

static int StrEqIgnoreCase(const char *A, const char *B) {
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

static void CopyName(char *Dst, int Max, const char *Src) {
    int i;
    if (Max <= 0) {
        return;
    }
    for (i = 0; Src[i] && i < Max - 1; i++) {
        Dst[i] = Src[i];
    }
    Dst[i] = 0;
}

int FileSystemActivate(int VolIdx) {
    if (VolIdx < 0 || VolIdx >= gVolCount) {
        return FAT_ERR_INVAL;
    }
    if (gActiveVol == VolIdx &&
        gActiveDrive == gVols[VolIdx].Drive &&
        gActiveLba == gVols[VolIdx].StartLba) {
        return FAT_OK;
    }
    if (!BlockSelect(gVols[VolIdx].Drive)) {
        return FAT_ERR_IO;
    }
    if (FatInit(gVols[VolIdx].StartLba) != FAT_OK) {
        gActiveVol = -1;
        return FAT_ERR_IO;
    }
    gActiveVol = VolIdx;
    gActiveDrive = gVols[VolIdx].Drive;
    gActiveLba = gVols[VolIdx].StartLba;
    return FAT_OK;
}

/*
 * 解析前缀：
 *   TOYOS:path / ESP:path / A:path / B:path
 *   0:path / 1:path（卷下标）
 * 无冒号 → 默认卷，整串为相对路径。
 */
int FileSystemResolve(const char *Path, int *OutVol, const char **OutRel) {
    const char *Colon;
    char Pref[FS_VOL_NAME_MAX];
    int PrefLen;
    int i;
    int Vol;

    if (!Path || !OutVol || !OutRel) {
        return FAT_ERR_INVAL;
    }
    if (gVolCount <= 0) {
        return FAT_ERR_IO;
    }

    Colon = Path;
    while (*Colon && *Colon != ':') {
        Colon++;
    }
    if (*Colon != ':') {
        *OutVol = gDefaultVol;
        *OutRel = Path;
        return FAT_OK;
    }

    PrefLen = (int)(Colon - Path);
    if (PrefLen <= 0 || PrefLen >= FS_VOL_NAME_MAX) {
        return FAT_ERR_INVAL;
    }
    for (i = 0; i < PrefLen; i++) {
        Pref[i] = Path[i];
    }
    Pref[PrefLen] = 0;

    Vol = -1;
    if (PrefLen == 1 && Pref[0] >= '0' && Pref[0] <= '9') {
        Vol = Pref[0] - '0';
    } else if (PrefLen == 1 &&
               ((Pref[0] >= 'A' && Pref[0] <= 'Z') ||
                (Pref[0] >= 'a' && Pref[0] <= 'z'))) {
        char L = Pref[0];
        if (L >= 'a') {
            L = (char)(L - 'a' + 'A');
        }
        for (i = 0; i < gVolCount; i++) {
            if (gVols[i].Letter == L) {
                Vol = i;
                break;
            }
        }
    } else {
        for (i = 0; i < gVolCount; i++) {
            if (StrEqIgnoreCase(Pref, gVols[i].Name)) {
                Vol = i;
                break;
            }
        }
    }
    if (Vol < 0 || Vol >= gVolCount) {
        return FAT_ERR_NOENT;
    }

    *OutVol = Vol;
    *OutRel = Colon + 1;
    if (**OutRel == '/' || **OutRel == '\\') {
        (*OutRel)++;
    }
    return FAT_OK;
}

static int FsPrepare(const char *Path, const char **RelOut, int NeedWrite) {
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

int FsListDir(const char *Path) {
    const char *Rel;
    int Err = FsPrepare(Path ? Path : "", &Rel, 0);
    if (Err != FAT_OK) {
        return Err;
    }
    if (!Path || Path[0] == 0) {
        Rel = 0;
    }
    return FatListDir(Rel && Rel[0] ? Rel : 0);
}

int FsListEntries(const char *Path, FAT_DIR_ENT *Out, int Max, int *OutCount) {
    const char *Rel;
    int Err = FsPrepare(Path ? Path : "", &Rel, 0);
    if (Err != FAT_OK) {
        return Err;
    }
    if (!Path || Path[0] == 0) {
        Rel = 0;
    }
    return FatListEntries(Rel && Rel[0] ? Rel : 0, Out, Max, OutCount);
}

int FsReadFile(const char *Path, void *Buffer, UINTN MaxSize, UINTN *OutSize) {
    const char *Rel;
    int Err = FsPrepare(Path, &Rel, 0);
    if (Err != FAT_OK) {
        return Err;
    }
    return FatReadFile(Rel, Buffer, MaxSize, OutSize);
}

int FsWriteFile(const char *Path, const void *Buffer, UINTN Size) {
    const char *Rel;
    int Err = FsPrepare(Path, &Rel, 1);
    if (Err != FAT_OK) {
        return Err;
    }
    return FatWriteFile(Rel, Buffer, Size);
}

int FsDeleteFile(const char *Path) {
    const char *Rel;
    int Err = FsPrepare(Path, &Rel, 1);
    if (Err != FAT_OK) {
        return Err;
    }
    return FatDeleteFile(Rel);
}

int FsMkdir(const char *Path) {
    const char *Rel;
    int Err = FsPrepare(Path, &Rel, 1);
    if (Err != FAT_OK) {
        return Err;
    }
    return FatMkdir(Rel);
}

int FsRmdir(const char *Path) {
    const char *Rel;
    int Err = FsPrepare(Path, &Rel, 1);
    if (Err != FAT_OK) {
        return Err;
    }
    return FatRmdir(Rel);
}

int FsRename(const char *OldPath, const char *NewPath) {
    const char *OldRel;
    const char *NewRel;
    int OldVol;
    int NewVol;
    int Err;

    if (!OldPath || !NewPath) {
        return FAT_ERR_INVAL;
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
    Err = FsPrepare(OldPath, &OldRel, 1);
    if (Err != FAT_OK) {
        return Err;
    }
    /* 同卷已 Activate；NewRel 相对路径 */
    Err = FileSystemResolve(NewPath, &NewVol, &NewRel);
    if (Err != FAT_OK) {
        return Err;
    }
    return FatRename(OldRel, NewRel);
}

int FileSystemVolCount(void) {
    return gVolCount;
}

int FileSystemDefaultVol(void) {
    return gDefaultVol;
}

int FileSystemVolInfo(int Idx, char *Name, int NameMax, UINT32 *Drive,
                      UINT32 *StartLba, int *ReadOnly) {
    if (Idx < 0 || Idx >= gVolCount) {
        return -1;
    }
    if (Name && NameMax > 0) {
        CopyName(Name, NameMax, gVols[Idx].Name);
    }
    if (Drive) {
        *Drive = gVols[Idx].Drive;
    }
    if (StartLba) {
        *StartLba = gVols[Idx].StartLba;
    }
    if (ReadOnly) {
        *ReadOnly = gVols[Idx].ReadOnly;
    }
    return 0;
}

static void CommandLs(int Argc, char **Argv) {
    const char *Path = (Argc >= 2) ? Argv[1] : 0;
    int Err = FsListDir(Path);
    if (Err != FAT_OK) {
        FatReport("ls", Err);
    }
}

static void CommandCat(int Argc, char **Argv) {
    static UINT8 Buf[4096];
    UINTN Size = 0;
    int Err;

    if (Argc < 2) {
        ConsoleWrite("usage: cat <file>\n");
        return;
    }
    Err = FsReadFile(Argv[1], Buf, sizeof(Buf) - 1, &Size);
    if (Err != FAT_OK) {
        FatReport("cat", Err);
        return;
    }
    Buf[Size] = 0;
    ConsoleWrite((const char *)Buf);
    if (Size > 0 && Buf[Size - 1] != '\n') {
        ConsoleWrite("\n");
    }
}

static void CommandWrite(int Argc, char **Argv) {
    static char Buf[512];
    UINTN Len = 0;
    int a;
    int first = 1;
    int Err;

    if (Argc < 3) {
        ConsoleWrite("usage: write <file> <text...>\n");
        return;
    }
    for (a = 2; a < Argc; a++) {
        const char *S = Argv[a];
        if (!first) {
            if (Len + 1 >= sizeof(Buf)) {
                break;
            }
            Buf[Len++] = ' ';
        }
        first = 0;
        while (*S && Len + 1 < sizeof(Buf)) {
            Buf[Len++] = *S++;
        }
    }
    if (Len + 1 < sizeof(Buf)) {
        Buf[Len++] = '\n';
    }
    Buf[Len] = 0;
    Err = FsWriteFile(Argv[1], Buf, Len);
    if (Err != FAT_OK) {
        FatReport("write", Err);
        return;
    }
    ConsoleWrite("write: ok ");
    ConsoleHex32((UINT32)Len);
    ConsoleWrite(" bytes\n");
}

static void CommandRm(int Argc, char **Argv) {
    int Err;

    if (Argc < 2) {
        ConsoleWrite("usage: rm <file>\n");
        return;
    }
    Err = FsDeleteFile(Argv[1]);
    if (Err != FAT_OK) {
        FatReport("rm", Err);
        return;
    }
    ConsoleWrite("rm: ok\n");
}

static void CommandMkdir(int Argc, char **Argv) {
    int Err;

    if (Argc < 2) {
        ConsoleWrite("usage: mkdir <dir>\n");
        return;
    }
    Err = FsMkdir(Argv[1]);
    if (Err != FAT_OK) {
        FatReport("mkdir", Err);
        return;
    }
    ConsoleWrite("mkdir: ok\n");
}

static void CommandRmdir(int Argc, char **Argv) {
    int Err;

    if (Argc < 2) {
        ConsoleWrite("usage: rmdir <dir>\n");
        return;
    }
    Err = FsRmdir(Argv[1]);
    if (Err != FAT_OK) {
        FatReport("rmdir", Err);
        return;
    }
    ConsoleWrite("rmdir: ok\n");
}

static void CommandMv(int Argc, char **Argv) {
    int Err;

    if (Argc < 3) {
        ConsoleWrite("usage: mv <old> <new>\n");
        return;
    }
    Err = FsRename(Argv[1], Argv[2]);
    if (Err != FAT_OK) {
        FatReport("mv", Err);
        return;
    }
    ConsoleWrite("mv: ok\n");
}

/* PR-FS2：列出已挂载卷 */
static void CommandVols(int Argc, char **Argv) {
    int i;
    (void)Argc;
    (void)Argv;

    if (gVolCount <= 0) {
        ConsoleWrite("vols: none\n");
        return;
    }
    for (i = 0; i < gVolCount; i++) {
        ConsoleWrite(i == gDefaultVol ? "* " : "  ");
        {
            char Let[3];
            Let[0] = gVols[i].Letter;
            Let[1] = ':';
            Let[2] = 0;
            ConsoleWrite(Let);
        }
        ConsoleWrite(" ");
        ConsoleWrite(gVols[i].Name);
        ConsoleWrite(":");
        ConsoleWrite(" drive=");
        ConsoleHex32(gVols[i].Drive);
        ConsoleWrite(" lba=");
        ConsoleHex32(gVols[i].StartLba);
        if (gVols[i].ReadOnly) {
            ConsoleWrite(" ro");
        }
        if (gVols[i].HasToyId) {
            ConsoleWrite(" toyos");
        }
        ConsoleWrite("\n");
    }
}

static int TryProbeDrive(UINT32 Drive, UINT32 *OutLba, int *OutIsEsp) {
    if (!BlockSelect(Drive)) {
        return 0;
    }
    return GptFindFatStartEx(OutLba, OutIsEsp);
}

/*
 * 扫描所有 Block 盘，各挂一个 FAT 卷；命名 A/B/…，
 * 含 TOYOS.ID 的另名 TOYOS（默认）；GPT ESP 只读且可名 ESP。
 */
static int MountAllVolumes(void) {
    UINT32 d;
    UINT8 Tmp[64];
    UINTN Sz;
    int ToyVol = -1;

    gVolCount = 0;
    gDefaultVol = 0;
    gActiveVol = -1;
    gActiveDrive = 0xFFFFFFFFu;
    gActiveLba = 0xFFFFFFFFu;

    for (d = 0; d < BLOCK_MAX_DRIVES && gVolCount < FS_MAX_VOLUMES; d++) {
        UINT32 Start = 0;
        int IsEsp = 0;
        FS_VOLUME *V;
        int Idx;

        if (!TryProbeDrive(d, &Start, &IsEsp)) {
            continue;
        }
        if (!BlockSelect(d) || FatInit(Start) != FAT_OK) {
            continue;
        }

        Idx = gVolCount;
        V = &gVols[Idx];
        V->Drive = d;
        V->StartLba = Start;
        V->Letter = (char)('A' + Idx);
        V->ReadOnly = IsEsp ? 1 : 0;
        V->HasToyId = 0;
        V->Name[0] = V->Letter;
        V->Name[1] = 0;

        if (FatReadFile("TOYOS.ID", Tmp, sizeof(Tmp), &Sz) == FAT_OK) {
            V->HasToyId = 1;
            CopyName(V->Name, FS_VOL_NAME_MAX, "TOYOS");
            ToyVol = Idx;
        } else if (IsEsp) {
            CopyName(V->Name, FS_VOL_NAME_MAX, "ESP");
            V->ReadOnly = 1;
        }

        gVolCount++;
        gActiveVol = Idx;
        gActiveDrive = d;
        gActiveLba = Start;

        DebugWrite("fs: vol ");
        DebugWrite(V->Name);
        DebugWrite(" letter=");
        DebugHex32((UINT32)(UINT8)V->Letter);
        DebugWrite(" drive=");
        DebugHex32(d);
        DebugWrite("\n");
    }

    if (gVolCount <= 0) {
        return 0;
    }
    if (ToyVol >= 0) {
        int i;
        gDefaultVol = ToyVol;
        /* 有 TOYOS 卷时，其余卷视为启动/ESP：只读，名 ESP（仍可用 A:） */
        for (i = 0; i < gVolCount; i++) {
            if (!gVols[i].HasToyId) {
                gVols[i].ReadOnly = 1;
                if (!(gVols[i].Name[0] && gVols[i].Name[1])) {
                    CopyName(gVols[i].Name, FS_VOL_NAME_MAX, "ESP");
                } else if (gVols[i].Name[0] == gVols[i].Letter &&
                           gVols[i].Name[1] == 0) {
                    CopyName(gVols[i].Name, FS_VOL_NAME_MAX, "ESP");
                }
            }
        }
    } else {
        gDefaultVol = 0;
    }
    if (FileSystemActivate(gDefaultVol) != FAT_OK) {
        return 0;
    }

    HalConsoleWriteSerial("fs: mounted ");
    {
        char Msg[8];
        Msg[0] = (char)('0' + gVolCount);
        Msg[1] = 0;
        HalConsoleWriteSerial(Msg);
    }
    HalConsoleWriteSerial(" volume(s), default=");
    HalConsoleWriteSerial(gVols[gDefaultVol].Name);
    HalConsoleWriteSerial("\n");
    return 1;
}

int FileSystemInit(void) {
    if (HalBlockInit() <= 0) {
        return -1;
    }
    if (!MountAllVolumes()) {
        return -1;
    }
    ConsoleRegister("ls", "list directory (TOYOS: / A:)", CommandLs);
    ConsoleRegister("cat", "print file", CommandCat);
    ConsoleRegister("write", "write file text", CommandWrite);
    ConsoleRegister("rm", "remove file or empty dir", CommandRm);
    ConsoleRegister("mkdir", "create directory", CommandMkdir);
    ConsoleRegister("rmdir", "remove empty directory", CommandRmdir);
    ConsoleRegister("mv", "rename/move file or dir", CommandMv);
    ConsoleRegister("vols", "list mounted volumes", CommandVols);
    DebugWrite("FS ready (ls, cat, write, rm, mkdir, rmdir, mv, vols)\n");
    return 0;
}
