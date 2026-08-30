/*
 * Fs.c — 文件系统模块组装
 *
 * 初始化磁盘 → 找 FAT 分区 → 挂载 FAT → 注册 Shell 命令。
 */
#include "Fs.h"
#include "Ata.h"
#include "Gpt.h"
#include "Fat.h"
#include "Console.h"

/* Shell 命令 ls：列出 FAT 根目录 */
static void CmdLs(int Argc, char **Argv) {
    (void)Argc;
    (void)Argv;
    FatListRoot();
}

/* Shell 命令 cat：读取根目录文件内容并打印 */
static void CmdCat(int Argc, char **Argv) {
    if (Argc < 2) {
        ConsoleWrite("usage: cat <file>\n");
        return;
    }
    static UINT8 Buf[4096];
    UINTN Size = 0;
    if (!FatReadFile(Argv[1], Buf, sizeof(Buf) - 1, &Size)) {
        ConsoleWrite("cat: not found\n");
        return;
    }
    Buf[Size] = 0;
    ConsoleWrite((const char *)Buf);
    if (Size > 0 && Buf[Size - 1] != '\n') {
        ConsoleWrite("\n");
    }
}

/* 初始化完整文件系统栈 */
int FsInit(void) {
    UINT32 Start = 0;
    if (!AtaInit()) {
        return -1;
    }
    if (!GptFindFatStart(&Start)) {
        return -1;
    }
    if (!FatInit(Start)) {
        return -1;
    }
    ConsoleRegister("ls", "list root directory", CmdLs);
    ConsoleRegister("cat", "print file", CmdCat);
    ConsoleWrite("FS ready (ls, cat)\n");
    return 0;
}
