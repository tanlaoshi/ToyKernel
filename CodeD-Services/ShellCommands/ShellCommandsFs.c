/*
 * ShellCommandsFs.c — PR-S3-shellfs-1：ls/cat/write/rm/mkdir/rmdir/mv + 注册汇总
 *
 * wrbig/vols/filestat/… 见 ShellCommandsFsExtra.c；install 见 ShellCommandsInstall.c。
 */
#include "ShellCommands.h"
#include "ShellPrivate.h"
#include "FileSystem.h"
#include "Fat.h"
#include "Console.h"

void ShellFatReport(const char *Cmd, int Err) {
    ConsoleWrite(Cmd);
    ConsoleWrite(": ");
    ConsoleWrite(FatStrError(Err));
    ConsoleWrite("\n");
}

static void CommandLs(int Argc, char **Argv) {
    const char *Path = (Argc >= 2) ? Argv[1] : 0;
    int Err = FileSystemListDirectory(Path);
    if (Err != FAT_OK) {
        ShellFatReport("ls", Err);
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
        ShellFatReport("cat", Err);
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
        ShellFatReport("write", Err);
        return;
    }
    ConsoleWrite("write: ok ");
    ConsoleWriteHex32((UINT32)Len);
    ConsoleWrite(" bytes\n");
}

static void CommandRm(int Argc, char **Argv) {
    int Err;

    if (Argc < 2) {
        ConsoleWrite("usage: rm <file>\n");
        return;
    }
    Err = FileSystemDeleteFile(Argv[1]);
    if (Err != FAT_OK) {
        ShellFatReport("rm", Err);
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
    /* PR-S-bundle-fs：mkdir 走 MakePath，支持 Apps/id/Assets 多级 */
    Err = FileSystemMakePath(Argv[1]);
    if (Err != FAT_OK) {
        ShellFatReport("mkdir", Err);
        return;
    }
    ConsoleWrite("mkdir: ok\n");
}

static void CommandRmdir(int Argc, char **Argv) {
    int Err;

    if (Argc < 2) {
        ConsoleWrite("usage: rmdir <dir> | rmdir -r <dir>\n");
        return;
    }
    if (Argc >= 3 && Argv[1][0] == '-' && Argv[1][1] == 'r' && Argv[1][2] == 0) {
        Err = FileSystemRemoveTree(Argv[2]);
    } else {
        Err = FileSystemRemoveDirectory(Argv[1]);
    }
    if (Err != FAT_OK) {
        ShellFatReport("rmdir", Err);
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
        ShellFatReport("mv", Err);
        return;
    }
    ConsoleWrite("mv: ok\n");
}

void ShellCommandsRegisterFs(void) {
    /* list：默认列目录；二级 tasks/devices 已由 ShellCommandsRegister 挂上 */
    ConsoleRegister("list", "list directory (TOYOS: / A: / RES:)", CommandLs);
    ConsoleRegisterAlias("list", "ls");
    ConsoleRegisterAlias("list", "dir");

    ConsoleRegister("print", "print file", CommandCat);
    ConsoleRegisterAlias("print", "cat");
    ConsoleRegisterAlias("print", "type");

    ConsoleRegister("write", "write file text", CommandWrite);

    ConsoleRegister2("make", "directory", "create directory", CommandMkdir);
    ConsoleRegisterAliasLine("mkdir", "make", "directory");
    ConsoleRegisterAliasLine("md", "make", "directory");

    ConsoleRegister("remove", "remove file or empty dir", CommandRm);
    ConsoleRegister2("remove", "directory", "remove empty directory", CommandRmdir);
    ConsoleRegisterAlias("remove", "rm");
    ConsoleRegisterAlias("remove", "del");
    ConsoleRegisterAliasLine("rmdir", "remove", "directory");
    ConsoleRegisterAliasLine("rd", "remove", "directory");

    ConsoleRegister("move", "rename/move file or dir", CommandMv);
    ConsoleRegisterAlias("move", "mv");
    ConsoleRegisterAlias("move", "rename");

    ShellCommandsFsExtraRegister();
    ShellCommandsInstallRegister();
}
