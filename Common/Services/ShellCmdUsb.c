/*
 * ShellCmdUsb.c — PR-S-shell-split-1：xhci / msc Shell 命令
 *
 * 从 ShellCommands.c 原样搬家；不改语义。
 */
#include "ShellPriv.h"
#include "Console.h"
#include "Hal.h"

/* PR-H-xhci-stat：Shell 可查 mode= + t/i/k/m…（与 PHOTO 同行格式） */
static void CommandXhci(int Argc, char **Argv) {
    char Diag[120];

    (void)Argc;
    (void)Argv;
    Diag[0] = 0;
    HalInputDiagFormat(Diag, (int)sizeof(Diag));
    ConsoleWrite("xhci ");
    ConsoleWrite(Diag[0] ? Diag : "(no stats)");
    ConsoleWrite("\n");
}

/* PR-H-msc-5：msc | msc scan | msc claim | msc capacity；不自动认盘、不挂 FAT */
static void CommandMsc(int Argc, char **Argv) {
    int Rc;

    if (Argc >= 2 && Argv[1] && Argv[1][0] == 's' && Argv[1][1] == 'c' &&
        Argv[1][2] == 'a' && Argv[1][3] == 'n' && Argv[1][4] == 0) {
        Rc = HalUsbMscScan();
        ConsoleWrite("msc: scan ");
        if (Rc < 0) {
            ConsoleWrite("fail (no hc)\n");
        } else {
            ConsoleWrite("ok n=");
            ConsoleWriteHex32((UINT32)Rc);
            ConsoleWrite(" (PORTSC only; no Address; claim for class/BOT)\n");
        }
        return;
    }

    if (Argc >= 2 && Argv[1] && Argv[1][0] == 'c' && Argv[1][1] == 'l' &&
        Argv[1][2] == 'a' && Argv[1][3] == 'i' && Argv[1][4] == 'm' &&
        Argv[1][5] == 0) {
        Rc = HalUsbMscClaim();
        ConsoleWrite("msc: claim ");
        if (Rc < 0) {
            ConsoleWrite("fail (no hc)\n");
        } else if (Rc == 0) {
            ConsoleWrite("none (no MSC bulk port; HID untouched)\n");
        } else {
            ConsoleWrite("ok ready=");
            ConsoleWrite(HalUsbMscReady() ? "1" : "0");
            ConsoleWrite(" (SetConfig+Bulk; use: msc capacity; HID untouched)\n");
        }
        return;
    }

    if (Argc >= 2 && Argv[1] && Argv[1][0] == 'c' && Argv[1][1] == 'a' &&
        Argv[1][2] == 'p' && Argv[1][3] == 'a' && Argv[1][4] == 'c' &&
        Argv[1][5] == 'i' && Argv[1][6] == 't' && Argv[1][7] == 'y' &&
        Argv[1][8] == 0) {
        Rc = HalUsbMscCapacity();
        ConsoleWrite("msc: capacity ");
        if (Rc < 0) {
            ConsoleWrite("fail (need claim; no FAT)\n");
        } else {
            ConsoleWrite("ok blocks=");
            ConsoleWriteHex32(HalUsbMscBlockCount());
            ConsoleWrite(" bsize=");
            ConsoleWriteHex32(HalUsbMscBlockSize());
            ConsoleWrite(" (INQUIRY+READ CAPACITY; no partition/FAT; HID untouched)\n");
        }
        return;
    }

    Rc = HalUsbMscInit();
    ConsoleWrite("msc: bringup=");
    ConsoleWrite(Rc == 0 ? "ok" : "fail");
    ConsoleWrite(" ready=");
    ConsoleWrite(HalUsbMscReady() ? "1" : "0");
    ConsoleWrite(" (use: msc scan | msc claim | msc capacity)\n");
}

void ShellCmdUsbRegister(void) {
    ConsoleRegister2("show", "xhci", "xHCI mode= + PHOTO counters", CommandXhci);
    ConsoleRegister("msc", "USB MSC: msc | msc scan | msc claim | msc capacity (PR-H-msc-5)", CommandMsc);
    ConsoleRegisterAliasLine("xhci", "show", "xhci");
}
