/*
 * FileSystem.c — 文件系统模块（Block + GPT + FAT）
 */
#include "FileSystem.h"
#include "Block.h"
#include "Gpt.h"
#include "Fat.h"
#include "Console.h"
#include "Debug.h"
#include "Hal.h"

static void FatReport(const char *Cmd, int Err) {
    ConsoleWrite(Cmd);
    ConsoleWrite(": ");
    ConsoleWrite(FatStrError(Err));
    ConsoleWrite("\n");
}

/* Shell 命令 ls：列出 FAT 目录（可选路径） */
static void CommandLs(int Argc, char **Argv) {
    const char *Path = (Argc >= 2) ? Argv[1] : 0;
    int Err = FatListDir(Path);
    if (Err != FAT_OK) {
        FatReport("ls", Err);
    }
}

/* Shell 命令 cat：读取文件内容并打印（支持 DIR/FILE） */
static void CommandCat(int Argc, char **Argv) {
    static UINT8 Buf[4096];
    UINTN Size = 0;
    int Err;

    if (Argc < 2) {
        ConsoleWrite("usage: cat <file>\n");
        return;
    }
    Err = FatReadFile(Argv[1], Buf, sizeof(Buf) - 1, &Size);
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

/* Shell 命令 write：write <file> <text...> 写入/覆盖文件（支持 DIR/FILE） */
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
    Err = FatWriteFile(Argv[1], Buf, Len);
    if (Err != FAT_OK) {
        FatReport("write", Err);
        return;
    }
    ConsoleWrite("write: ok ");
    ConsoleHex32((UINT32)Len);
    ConsoleWrite(" bytes\n");
}

/* Shell 命令 rm：删除文件或空目录 */
static void CommandRm(int Argc, char **Argv) {
    int Err;

    if (Argc < 2) {
        ConsoleWrite("usage: rm <file>\n");
        return;
    }
    Err = FatDeleteFile(Argv[1]);
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
    Err = FatMkdir(Argv[1]);
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
    Err = FatRmdir(Argv[1]);
    if (Err != FAT_OK) {
        FatReport("rmdir", Err);
        return;
    }
    ConsoleWrite("rmdir: ok\n");
}

/* 在当前 Block 盘上挂载 FAT；成功返回 1 */
static int TryMountCurrent(void) {
    UINT32 Start = 0;
    if (!GptFindFatStart(&Start)) {
        return 0;
    }
    if (FatInit(Start) != FAT_OK) {
        return 0;
    }
    return 1;
}

/*
 * 优先挂载带 TOYOS.ID 的卷（独立系统盘），
 * 其次含 HELLO.ELF/COUNT.ELF 且驱动器号更大的卷（避免 vvfat:. 启动盘抢走），
 * 否则退回第一块可用 FAT。
 */
static int MountBestFat(void) {
    UINT32 BestDrive = 0;
    UINT32 Found = 0;
    UINT32 d;
    UINT8 Tmp[64];
    UINTN Sz;
    int ScoreBest = -1;

    for (d = 0; d < BLOCK_MAX_DRIVES; d++) {
        int Score;

        if (!BlockSelect(d)) {
            continue;
        }
        if (!TryMountCurrent()) {
            continue;
        }
        if (!Found) {
            BestDrive = d;
            Found = 1;
            ScoreBest = 0;
        }

        Score = 0;
        if (FatReadFile("TOYOS.ID", Tmp, sizeof(Tmp), &Sz) == FAT_OK) {
            Score = 100 + (int)d;
        } else if (FatReadFile("HELLO.ELF", Tmp, sizeof(Tmp), &Sz) == FAT_OK ||
                   FatReadFile("COUNT.ELF", Tmp, sizeof(Tmp), &Sz) == FAT_OK) {
            /* 同有用户程序时偏向从盘，避开 fat:rw:. 启动目录 */
            Score = 10 + (int)d;
        }
        if (Score > ScoreBest) {
            ScoreBest = Score;
            BestDrive = d;
        }
    }

    if (!Found) {
        return 0;
    }
    if (!BlockSelect(BestDrive) || !TryMountCurrent()) {
        return 0;
    }
    /* Gui 尚未 Init：勿 ConsoleWrite 上屏，否则 USB 等模块耗时期间灰桌面顶行残留 */
    HalConsoleWriteSerial("fs: mounted\n");
    DebugWrite("fs: mounted drive ");
    DebugHex32(BestDrive);
    DebugWrite(" score=");
    DebugHex32((UINT32)ScoreBest);
    DebugWrite("\n");
    return 1;
}

/* 初始化完整文件系统栈 */
int FileSystemInit(void) {
    if (HalBlockInit() <= 0) {
        return -1;
    }
    if (!MountBestFat()) {
        return -1;
    }
    ConsoleRegister("ls", "list directory", CommandLs);
    ConsoleRegister("cat", "print file", CommandCat);
    ConsoleRegister("write", "write file text", CommandWrite);
    ConsoleRegister("rm", "remove file or empty dir", CommandRm);
    ConsoleRegister("mkdir", "create directory", CommandMkdir);
    ConsoleRegister("rmdir", "remove empty directory", CommandRmdir);
    DebugWrite("FS ready (ls, cat, write, rm, mkdir, rmdir)\n");
    return 0;
}
