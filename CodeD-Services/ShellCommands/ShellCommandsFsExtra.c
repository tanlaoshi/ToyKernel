/*
 * ShellCommandsFsExtra.c — PR-S3-shellfs-1：wrbig / vols / filestat / filesync / dirstress
 */
#include "ShellCommands.h"
#include "ShellPrivate.h"
#include "FileSystem.h"
#include "Fat.h"
#include "Console.h"
#include "PhysicalMemory.h"

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
        ShellFatReport("wrbig", FAT_ERR_FILE_TOO_BIG);
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
        ShellFatReport("wrbig", Err);
        PhysicalMemoryFreePages(Buf, Pages);
        return;
    }
    for (i = 0; i < Size; i++) {
        Buf[i] = 0;
    }
    Err = FileSystemReadFile(Argv[1], Buf, Size, &OutSize);
    if (Err != FAT_OK) {
        ShellFatReport("wrbig", Err);
        PhysicalMemoryFreePages(Buf, Pages);
        return;
    }
    if (OutSize != Size) {
        ConsoleWrite("wrbig: fail size want=");
        ConsoleWriteHex32((UINT32)Size);
        ConsoleWrite(" got=");
        ConsoleWriteHex32((UINT32)OutSize);
        ConsoleWrite("\n");
        Ok = 0;
    } else {
        for (i = 0; i < Size; i++) {
            UINT8 Expect = (UINT8)((i * 131u + 17u) & 0xFFu);
            if (Buf[i] != Expect) {
                ConsoleWrite("wrbig: fail pattern at ");
                ConsoleWriteHex32((UINT32)i);
                ConsoleWrite("\n");
                Ok = 0;
                break;
            }
        }
    }
    PhysicalMemoryFreePages(Buf, Pages);
    if (Ok) {
        ConsoleWrite("wrbig: ok ");
        ConsoleWriteHex32((UINT32)Size);
        ConsoleWrite(" bytes\n");
    }
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
        ConsoleWriteHex32(Drive);
        ConsoleWrite(" lba=");
        ConsoleWriteHex32(StartLba);
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
        ShellFatReport("filestat", Err);
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
    ConsoleWriteHex32(St.Size);
    ConsoleWrite(" cluster=");
    ConsoleWriteHex32(St.Cluster);
    ConsoleWrite(" attr=");
    ConsoleWriteHex32((UINT32)St.Attr);
    ConsoleWrite("\n");
}

/* PR-F2：落盘同步（全称 filesync） */
static void CommandFileSync(int Argc, char **Argv) {
    const char *Path = (Argc >= 2) ? Argv[1] : "";
    int Err = FileSystemFileSync(Path);
    if (Err != FAT_OK) {
        ShellFatReport("filesync", Err);
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
        ShellFatReport("dirstress", Err);
        ConsoleWrite("dirstress: created=");
        ConsoleWriteHex32((UINT32)Created);
        ConsoleWrite(" grew=");
        ConsoleWrite(Grew ? "yes" : "no");
        ConsoleWrite("\n");
        return;
    }
    ConsoleWrite("dirstress: ok created=");
    ConsoleWriteHex32((UINT32)Created);
    ConsoleWrite(" grew=");
    ConsoleWrite(Grew ? "yes" : "no");
    ConsoleWrite("\n");
}

void ShellCommandsFsExtraRegister(void) {
    ConsoleRegister2("list", "volumes", "list mounted volumes", CommandVols);
    ConsoleRegisterAliasLine("vols", "list", "volumes");
    ConsoleRegisterAliasLine("volumes", "list", "volumes");

    ConsoleRegister2("write", "big", "write+verify large file", CommandWrbig);
    ConsoleRegisterAliasLine("wrbig", "write", "big");

    ConsoleRegister2("show", "file", "file/dir status", CommandFileStat);
    ConsoleRegisterAliasLine("filestat", "show", "file");
    ConsoleRegisterAliasLine("stat", "show", "file");

    ConsoleRegister2("sync", "file", "flush volume to disk", CommandFileSync);
    ConsoleRegisterAliasLine("filesync", "sync", "file");

    ConsoleRegister2("stress", "directory", "grow subdir clusters", CommandDirStress);
    ConsoleRegisterAliasLine("dirstress", "stress", "directory");
}
