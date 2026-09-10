/*
 * FileSystem.c — Block + GPT + VFS（PR-FS2 多卷；PR-F1 经 FsOps）
 */
#include "FileSystem.h"
#include "ShellCommands.h"
#include "Block.h"
#include "Gpt.h"
#include "Vfs.h"
#include "Debug.h"
#include "Hal.h"
#include "CoreOps.h"

typedef struct {
    UINT32 Drive;
    UINT32 StartLba;
    char   Name[FS_VOL_NAME_MAX]; /* 前缀名，不含冒号：TOYOS / A / ESP / RES */
    char   Letter;                /* 'A'+index，便于 A: 访问 */
    int    ReadOnly;
    int    HasToyId;
    const FS_OPS *Ops;            /* PR-F3：每卷后端（fat / res） */
} FS_VOLUME;

static FS_VOLUME gVols[FS_MAX_VOLUMES];
static int gVolCount;
static int gDefaultVol;
static int gActiveVol = -1;
static UINT32 gActiveDrive = 0xFFFFFFFFu;
static UINT32 gActiveLba = 0xFFFFFFFFu;
static const FS_OPS *gActiveOps;

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
    const FS_OPS *Ops;

    if (VolIdx < 0 || VolIdx >= gVolCount) {
        return FAT_ERR_INVAL;
    }
    Ops = gVols[VolIdx].Ops ? gVols[VolIdx].Ops : FatFsOps();
    if (gActiveVol == VolIdx &&
        gActiveOps == Ops &&
        gActiveDrive == gVols[VolIdx].Drive &&
        gActiveLba == gVols[VolIdx].StartLba) {
        return FAT_OK;
    }
    if (VfsSelect(Ops) != 0) {
        return FAT_ERR_IO;
    }
    if (!Ops->Synthetic) {
        if (!BlockSelect(gVols[VolIdx].Drive)) {
            gActiveVol = -1;
            gActiveOps = 0;
            return FAT_ERR_IO;
        }
    }
    if (VfsMount(gVols[VolIdx].StartLba) != FAT_OK) {
        gActiveVol = -1;
        gActiveOps = 0;
        return FAT_ERR_IO;
    }
    gActiveVol = VolIdx;
    gActiveOps = Ops;
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

static int PathIsAssetsRelative(const char *Path) {
    const char *Want = "assets/";
    int i;

    if (!Path) {
        return 0;
    }
    for (i = 0; Want[i]; i++) {
        char C = Path[i];
        if (C >= 'A' && C <= 'Z') {
            C = (char)(C - 'A' + 'a');
        }
        if (C == '\\') {
            C = '/';
        }
        if (C != Want[i]) {
            return 0;
        }
    }
    return 1;
}

int FileSystemReadFile(const char *Path, void *Buffer, UINTN MaxSize, UINTN *OutSize) {
    const char *Rel;
    int Err;
    char ResPath[160];
    int i;

    if (!Path) {
        return FAT_ERR_INVAL;
    }
    Err = FileSystemPreparePath(Path, &Rel, 0);
    if (Err == FAT_OK) {
        Err = VfsReadFile(Rel, Buffer, MaxSize, OutSize);
        if (Err == FAT_OK) {
            return FAT_OK;
        }
    }
    /*
     * 真机无 USB MSC 时默认卷常是 NVMe ESP，没有 Assets/。
     * 无卷前缀的 Assets/… 失败则回退 RES:（内嵌桌面图标/壁纸）。
     */
    if (!PathIsAssetsRelative(Path)) {
        return Err != FAT_OK ? Err : FAT_ERR_NOENT;
    }
    ResPath[0] = 'R';
    ResPath[1] = 'E';
    ResPath[2] = 'S';
    ResPath[3] = ':';
    for (i = 0; Path[i] && i < (int)sizeof(ResPath) - 5; i++) {
        ResPath[4 + i] = Path[i];
    }
    ResPath[4 + i] = 0;
    Err = FileSystemPreparePath(ResPath, &Rel, 0);
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

int FileSystemDeleteFile(const char *Path) {
    const char *Rel;
    int Err = FileSystemPreparePath(Path, &Rel, 1);
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

/* PR-F3：后端名（如 fat / res）；Out 可 NULL */
int FileSystemVolBackend(int Idx, const char **OutName) {
    const FS_OPS *Ops;
    if (Idx < 0 || Idx >= gVolCount) {
        return -1;
    }
    Ops = gVols[Idx].Ops ? gVols[Idx].Ops : FatFsOps();
    if (OutName) {
        *OutName = Ops->Name ? Ops->Name : "?";
    }
    return 0;
}

/*
 * 扫描所有 Block 盘，挂上每个盘上的全部 FAT 分区；
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
    gActiveOps = 0;
    gActiveDrive = 0xFFFFFFFFu;
    gActiveLba = 0xFFFFFFFFu;

    for (d = 0; d < BLOCK_MAX_DRIVES && gVolCount < FS_MAX_VOLUMES; d++) {
        GPT_FAT_PART Parts[GPT_MAX_FAT_PARTS];
        int N;
        int p;

        if (!BlockSelect(d)) {
            continue;
        }
        N = GptFindAllFat(Parts, GPT_MAX_FAT_PARTS);
        if (N <= 0) {
            continue;
        }
        for (p = 0; p < N && gVolCount < FS_MAX_VOLUMES; p++) {
            FS_VOLUME *V;
            int Idx;
            int IsEsp = Parts[p].IsEsp;
            UINT32 Start = Parts[p].StartLba;

            if (VfsSelect(FatFsOps()) != 0) {
                continue;
            }
            if (!BlockSelect(d) || VfsMount(Start) != FAT_OK) {
                continue;
            }

            Idx = gVolCount;
            V = &gVols[Idx];
            V->Drive = d;
            V->StartLba = Start;
            V->Letter = (char)('A' + Idx);
            V->ReadOnly = IsEsp ? 1 : 0;
            V->HasToyId = 0;
            V->Ops = FatFsOps();
            V->Name[0] = V->Letter;
            V->Name[1] = 0;

            if (VfsReadFile("TOYOS.ID", Tmp, sizeof(Tmp), &Sz) == FAT_OK) {
                V->HasToyId = 1;
                CopyName(V->Name, FS_VOL_NAME_MAX, "TOYOS");
                ToyVol = Idx;
            } else if (IsEsp) {
                CopyName(V->Name, FS_VOL_NAME_MAX, "ESP");
                V->ReadOnly = 1;
            }

            gVolCount++;
            gActiveVol = Idx;
            gActiveOps = V->Ops;
            gActiveDrive = d;
            gActiveLba = Start;

            DebugWrite("fs: vol ");
            DebugWrite(V->Name);
            DebugWrite(" letter=");
            DebugHex32((UINT32)(UINT8)V->Letter);
            DebugWrite(" drive=");
            DebugHex32(d);
            DebugWrite(" lba=");
            DebugHex32(Start);
            DebugWrite("\n");
        }
    }

    /* PR-F3：可选只读资源卷（无盘亦可挂；有盘时占下一字母） */
    if (gVolCount < FS_MAX_VOLUMES && ResFsOps()) {
        FS_VOLUME *V = &gVols[gVolCount];
        int Idx = gVolCount;

        V->Drive = 0xFFFFFFFEu;
        V->StartLba = 0;
        V->Letter = (char)('A' + Idx);
        V->ReadOnly = 1;
        V->HasToyId = 0;
        V->Ops = ResFsOps();
        CopyName(V->Name, FS_VOL_NAME_MAX, "RES");
        if (VfsSelect(V->Ops) == 0 && VfsMount(0) == FAT_OK) {
            gVolCount++;
            gActiveVol = Idx;
            gActiveOps = V->Ops;
            gActiveDrive = V->Drive;
            gActiveLba = 0;
            DebugWrite("fs: vol RES (resfs)\n");
            HalConsoleWriteSerial("fs: res volume RES:\n");
        }
    }

    if (gVolCount <= 0) {
        return 0;
    }
    if (ToyVol >= 0) {
        int i;
        gDefaultVol = ToyVol;
        /* 有 TOYOS 卷时，其余 Block 卷视为启动/ESP：只读，名 ESP（仍可用 A:） */
        for (i = 0; i < gVolCount; i++) {
            if (gVols[i].Ops && gVols[i].Ops->Synthetic) {
                continue; /* RES 等合成卷保持原名 */
            }
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
        int i;
        int FatVol = -1;
        int PrefVol = -1;
        int MarkerVol = -1;

        for (i = 0; i < gVolCount; i++) {
            if (!gVols[i].Ops || gVols[i].Ops->Synthetic) {
                continue;
            }
            if (FatVol < 0) {
                FatVol = i;
            }
            /* 无 TOYOS.ID 时优先非 ESP（可写数据分区） */
            if (!gVols[i].ReadOnly && PrefVol < 0) {
                PrefVol = i;
            }
            if (MarkerVol < 0 && FileSystemActivate(i) == FAT_OK) {
                if (VfsReadFile("HELLO.ELF", Tmp, sizeof(Tmp), &Sz) == FAT_OK ||
                    VfsReadFile("Kernel.elf", Tmp, sizeof(Tmp), &Sz) == FAT_OK ||
                    VfsReadFile("TOYOS.DB", Tmp, sizeof(Tmp), &Sz) == FAT_OK) {
                    MarkerVol = i;
                }
            }
        }
        if (MarkerVol >= 0) {
            gDefaultVol = MarkerVol;
        } else if (PrefVol >= 0) {
            gDefaultVol = PrefVol;
        } else {
            gDefaultVol = (FatVol >= 0) ? FatVol : 0;
        }
        if (gDefaultVol >= 0 && gDefaultVol < gVolCount &&
            gVols[gDefaultVol].Name[0] == 'E' && gVols[gDefaultVol].Name[1] == 'S' &&
            gVols[gDefaultVol].Name[2] == 'P') {
            HalConsoleWriteSerial(
                "fs: default=ESP (no TOYOS.ID; put rootfs on a FAT with TOYOS.ID)\n");
        }
    }
    if (FileSystemActivate(gDefaultVol) != FAT_OK) {
        return 0;
    }

    HalConsoleWriteSerial("fs: mounted ");
    {
        char Msg[8];
        Msg[0] = (char)('0' + (gVolCount > 9 ? 9 : gVolCount));
        Msg[1] = 0;
        HalConsoleWriteSerial(Msg);
    }
    HalConsoleWriteSerial(" volume(s), default=");
    HalConsoleWriteSerial(gVols[gDefaultVol].Name);
    HalConsoleWriteSerial("\n");
    return 1;
}

int FileSystemInit(void) {
    VFS_SERVICE_OPS Svc;

    if (VfsRegister(FatFsOps()) != 0) {
        DebugWrite("FS: VfsRegister(fat) failed\n");
        return 0;
    }
    if (VfsRegister(ResFsOps()) != 0) {
        DebugWrite("FS: VfsRegister(res) failed\n");
        return 0;
    }
    if (HalBlockInit() <= 0) {
        DebugWrite("FS: no block device (RES-only possible)\n");
    }
    if (!MountAllVolumes()) {
        DebugWrite("FS: no volumes mounted\n");
        return 0;
    }
    Svc.ReadFile = FileSystemReadFile;
    Svc.WriteFile = FileSystemWriteFile;
    Svc.ListEntries = FileSystemListEntries;
    Svc.FileStat = FileSystemFileStat;
    VfsServiceOpsRegister(&Svc);
    ShellCommandsRegisterFs();
    DebugWrite("FS ready (ls, cat, write, wrbig, dirstress, rm, mkdir, rmdir, mv, vols, filestat, filesync)\n");
    return 0;
}
