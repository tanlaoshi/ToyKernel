/*
 * ShellCommandsFs.c — 文件系统 Shell 命令（PR-R3 自 FileSystem.c 迁出）
 */
#include "ShellCommands.h"
#include "FileSystem.h"
#include "Fat.h"
#include "Console.h"
#include "PhysicalMemory.h"

static void FatReport(const char *Cmd, int Err) {
    ConsoleWrite(Cmd);
    ConsoleWrite(": ");
    ConsoleWrite(FatStrError(Err));
    ConsoleWrite("\n");
}

static void CommandLs(int Argc, char **Argv) {
    const char *Path = (Argc >= 2) ? Argv[1] : 0;
    int Err = FileSystemListDirectory(Path);
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
    Err = FileSystemReadFile(Argv[1], Buf, sizeof(Buf) - 1, &Size);
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
    Err = FileSystemWriteFile(Argv[1], Buf, Len);
    if (Err != FAT_OK) {
        FatReport("write", Err);
        return;
    }
    ConsoleWrite("write: ok ");
    ConsoleHex32((UINT32)Len);
    ConsoleWrite(" bytes\n");
}

/* PR-FS3：大文件写验收（默认 ~2MiB，上限 FAT_WRITE_MAX） */
static void CommandWrbig(int Argc, char **Argv) {
    UINT32 SizeKb = 2048;
    UINTN Size;
    UINT32 Pages;
    UINT8 *Buf;
    UINTN i;
    UINTN OutSize = 0;
    int Err;
    int Ok = 1;

    if (Argc < 2) {
        ConsoleWrite("usage: wrbig <file> [size_kb]\n");
        return;
    }
    if (Argc >= 3) {
        SizeKb = 0;
        for (const char *P = Argv[2]; *P; P++) {
            if (*P < '0' || *P > '9') {
                ConsoleWrite("wrbig: bad size_kb\n");
                return;
            }
            SizeKb = SizeKb * 10u + (UINT32)(*P - '0');
        }
        if (SizeKb == 0) {
            ConsoleWrite("wrbig: size_kb must be > 0\n");
            return;
        }
    }
    if ((UINT64)SizeKb * 1024u > (UINT64)FAT_WRITE_MAX) {
        FatReport("wrbig", FAT_ERR_FBIG);
        return;
    }
    Size = (UINTN)SizeKb * 1024u;
    Pages = (UINT32)((Size + PAGE_SIZE - 1) / PAGE_SIZE);
    Buf = (UINT8 *)PhysicalMemoryAllocatePages(Pages);
    if (!Buf) {
        ConsoleWrite("wrbig: alloc failed\n");
        return;
    }
    for (i = 0; i < Size; i++) {
        Buf[i] = (UINT8)((i * 131u + 17u) & 0xFFu);
    }
    Err = FileSystemWriteFile(Argv[1], Buf, Size);
    if (Err != FAT_OK) {
        FatReport("wrbig", Err);
        PhysicalMemoryFreePages(Buf, Pages);
        return;
    }
    for (i = 0; i < Size; i++) {
        Buf[i] = 0;
    }
    Err = FileSystemReadFile(Argv[1], Buf, Size, &OutSize);
    if (Err != FAT_OK) {
        FatReport("wrbig", Err);
        PhysicalMemoryFreePages(Buf, Pages);
        return;
    }
    if (OutSize != Size) {
        ConsoleWrite("wrbig: fail size want=");
        ConsoleHex32((UINT32)Size);
        ConsoleWrite(" got=");
        ConsoleHex32((UINT32)OutSize);
        ConsoleWrite("\n");
        Ok = 0;
    } else {
        for (i = 0; i < Size; i++) {
            UINT8 Expect = (UINT8)((i * 131u + 17u) & 0xFFu);
            if (Buf[i] != Expect) {
                ConsoleWrite("wrbig: fail pattern at ");
                ConsoleHex32((UINT32)i);
                ConsoleWrite("\n");
                Ok = 0;
                break;
            }
        }
    }
    PhysicalMemoryFreePages(Buf, Pages);
    if (Ok) {
        ConsoleWrite("wrbig: ok ");
        ConsoleHex32((UINT32)Size);
        ConsoleWrite(" bytes\n");
    }
}

static void CommandRm(int Argc, char **Argv) {
    int Err;

    if (Argc < 2) {
        ConsoleWrite("usage: rm <file>\n");
        return;
    }
    Err = FileSystemDeleteFile(Argv[1]);
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
    Err = FileSystemMakeDirectory(Argv[1]);
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
    Err = FileSystemRemoveDirectory(Argv[1]);
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
    Err = FileSystemRename(Argv[1], Argv[2]);
    if (Err != FAT_OK) {
        FatReport("mv", Err);
        return;
    }
    ConsoleWrite("mv: ok\n");
}

static void CommandVols(int Argc, char **Argv) {
    int i;
    int Count;
    (void)Argc;
    (void)Argv;

    Count = FileSystemVolCount();
    if (Count <= 0) {
        ConsoleWrite("vols: none\n");
        return;
    }
    for (i = 0; i < Count; i++) {
        char Name[FS_VOL_NAME_MAX];
        UINT32 Drive = 0;
        UINT32 StartLba = 0;
        int ReadOnly = 0;
        char Let[3];

        if (FileSystemVolInfo(i, Name, sizeof(Name), &Drive, &StartLba, &ReadOnly) != 0) {
            continue;
        }
        ConsoleWrite(i == FileSystemDefaultVol() ? "* " : "  ");
        Let[0] = (char)('A' + i);
        Let[1] = ':';
        Let[2] = 0;
        ConsoleWrite(Let);
        ConsoleWrite(" ");
        ConsoleWrite(Name);
        ConsoleWrite(":");
        ConsoleWrite(" drive=");
        ConsoleHex32(Drive);
        ConsoleWrite(" lba=");
        ConsoleHex32(StartLba);
        if (ReadOnly) {
            ConsoleWrite(" ro");
        }
        {
            const char *Backend = 0;
            if (FileSystemVolBackend(i, &Backend) == 0 && Backend) {
                ConsoleWrite(" ");
                ConsoleWrite(Backend);
            }
        }
        {
            const char *N = Name;
            int IsToy = 1;
            const char *T = "TOYOS";
            while (*T) {
                char Ca = *N;
                char Cb = *T;
                if (Ca >= 'a' && Ca <= 'z') {
                    Ca = (char)(Ca - 'a' + 'A');
                }
                if (Ca != Cb) {
                    IsToy = 0;
                    break;
                }
                N++;
                T++;
            }
            if (IsToy && *N == 0) {
                ConsoleWrite(" toyos");
            }
        }
        ConsoleWrite("\n");
    }
}

/* PR-F2：文件状态查询（全称 filestat，勿用孤立 stat） */
static void CommandFileStat(int Argc, char **Argv) {
    FAT_FILE_STAT St;
    int Err;
    const char *Path;

    if (Argc < 2) {
        ConsoleWrite("usage: filestat <path>\n");
        return;
    }
    Path = Argv[1];
    Err = FileSystemFileStat(Path, &St);
    if (Err != FAT_OK) {
        FatReport("filestat", Err);
        return;
    }
    ConsoleWrite(Path);
    ConsoleWrite(": ");
    if (St.Attr & FAT_ATTR_DIR) {
        ConsoleWrite("dir");
    } else {
        ConsoleWrite("file");
    }
    if (St.Attr & FAT_ATTR_RO) {
        ConsoleWrite(" ro");
    }
    ConsoleWrite(" size=");
    ConsoleHex32(St.Size);
    ConsoleWrite(" cluster=");
    ConsoleHex32(St.Cluster);
    ConsoleWrite(" attr=");
    ConsoleHex32((UINT32)St.Attr);
    ConsoleWrite("\n");
}

/* PR-F2：落盘同步（全称 filesync） */
static void CommandFileSync(int Argc, char **Argv) {
    const char *Path = (Argc >= 2) ? Argv[1] : "";
    int Err = FileSystemFileSync(Path);
    if (Err != FAT_OK) {
        FatReport("filesync", Err);
        return;
    }
    ConsoleWrite("filesync: ok\n");
}

/* PR-F3：大目录簇扩展回归 */
static void CommandDirStress(int Argc, char **Argv) {
    const char *Dir = "BIGDIR";
    int MaxFiles = 0; /* 0 = 约填 70% 簇；grow → 负值强制扩展 */
    int Created = 0;
    int Grew = 0;
    int Err;
    int i;
    int ForceGrow = 0;

    if (Argc >= 2) {
        Dir = Argv[1];
    }
    if (Argc >= 3) {
        if (Argv[2][0] == 'g' || Argv[2][0] == 'G') {
            /* grow — 强制 DirGrow（真 FAT；vvfat 可能崩） */
            ForceGrow = 1;
            MaxFiles = 0;
        } else {
            MaxFiles = 0;
            for (i = 0; Argv[2][i]; i++) {
                char C = Argv[2][i];
                if (C < '0' || C > '9') {
                    ConsoleWrite("usage: dirstress [dir] [count|grow]\n");
                    return;
                }
                MaxFiles = MaxFiles * 10 + (C - '0');
            }
            if (MaxFiles <= 0) {
                ConsoleWrite("dirstress: count must be > 0\n");
                return;
            }
        }
    }
    if (ForceGrow) {
        MaxFiles = -1;
    }
    Err = FileSystemDirStress(Dir, MaxFiles, &Created, &Grew);
    if (Err != FAT_OK) {
        FatReport("dirstress", Err);
        ConsoleWrite("dirstress: created=");
        ConsoleHex32((UINT32)Created);
        ConsoleWrite(" grew=");
        ConsoleWrite(Grew ? "yes" : "no");
        ConsoleWrite("\n");
        return;
    }
    ConsoleWrite("dirstress: ok created=");
    ConsoleHex32((UINT32)Created);
    ConsoleWrite(" grew=");
    ConsoleWrite(Grew ? "yes" : "no");
    ConsoleWrite("\n");
}

void ShellCommandsRegisterFs(void) {
    ConsoleRegister("ls", "list directory (TOYOS: / A: / RES:)", CommandLs);
    ConsoleRegister("cat", "print file", CommandCat);
    ConsoleRegister("write", "write file text", CommandWrite);
    ConsoleRegister("wrbig", "write+verify large file (PR-FS3)", CommandWrbig);
    ConsoleRegister("dirstress", "grow subdir clusters (PR-F3)", CommandDirStress);
    ConsoleRegister("rm", "remove file or empty dir", CommandRm);
    ConsoleRegister("mkdir", "create directory", CommandMkdir);
    ConsoleRegister("rmdir", "remove empty directory", CommandRmdir);
    ConsoleRegister("mv", "rename/move file or dir", CommandMv);
    ConsoleRegister("vols", "list mounted volumes", CommandVols);
    ConsoleRegister("filestat", "file/dir status (PR-F2 FileStat)", CommandFileStat);
    ConsoleRegister("filesync", "flush volume to disk (PR-F2 FileSync)", CommandFileSync);
}
