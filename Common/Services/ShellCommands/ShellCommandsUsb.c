/*
 * ShellCommandsUsb.c — PR-S-shell-split-1：xhci / msc Shell 命令
 *
 * 从 ShellCommands.c 原样搬家；msc mount = PR-H-msc-6。
 */
#include "ShellPrivate.h"
#include "Console.h"
#include "Hal.h"
#include "FileSystem.h"

/* PR-H-xhci-stat / PR-V-input-diag：Shell 可查 mode= 或 virtio 计数 */
static void CommandInputDiag(int Argc, char **Argv) {
    char Diag[160];

    (void)Argc;
    (void)Argv;
    Diag[0] = 0;
    HalInputDiagFormat(Diag, (int)sizeof(Diag));
    ConsoleWrite("input ");
    ConsoleWrite(Diag[0] ? Diag : "(no stats)");
    ConsoleWrite("\n");
}

static void CommandXhci(int Argc, char **Argv) {
    char Diag[160];

    (void)Argc;
    (void)Argv;
    Diag[0] = 0;
    HalInputDiagFormat(Diag, (int)sizeof(Diag));
    ConsoleWrite("xhci ");
    ConsoleWrite(Diag[0] ? Diag : "(no stats)");
    ConsoleWrite("\n");
}

/* PR-H-msc-6：msc | scan | claim | capacity | mount；不自动认盘 */
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
            ConsoleWrite(" (SetConfig+Bulk; use: msc capacity|mount; HID untouched)\n");
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

    if (Argc >= 2 && Argv[1] && Argv[1][0] == 'm' && Argv[1][1] == 'o' &&
        Argv[1][2] == 'u' && Argv[1][3] == 'n' && Argv[1][4] == 't' &&
        Argv[1][5] == 0) {
        Rc = HalUsbMscMount();
        ConsoleWrite("msc: mount ");
        if (Rc == -1) {
            ConsoleWrite("fail (need claim)\n");
            return;
        }
        if (Rc == -2) {
            ConsoleWrite("fail (capacity)\n");
            return;
        }
        if (Rc == -3) {
            ConsoleWrite("fail (bsize!=512; FAT needs 512)\n");
            return;
        }
        if (Rc != 0) {
            ConsoleWrite("fail\n");
            return;
        }
        ConsoleWrite("mux ok bsize=512; remount…\n");
        if (!FileSystemRemountVolumes()) {
            ConsoleWrite("msc: remount fail (no FAT vols; HID untouched)\n");
            return;
        }
        ConsoleWrite("msc: remount ok vols=");
        ConsoleWriteHex32((UINT32)FileSystemVolCount());
        ConsoleWrite(" default=");
        {
            char Name[FS_VOL_NAME_MAX];
            int Def = FileSystemDefaultVol();

            Name[0] = 0;
            if (Def >= 0) {
                (void)FileSystemVolInfo(Def, Name, FS_VOL_NAME_MAX, 0, 0, 0);
            }
            ConsoleWrite(Name[0] ? Name : "?");
        }
        ConsoleWrite(" (try: vols | ls TOYOS:; HID untouched)\n");
        return;
    }

    Rc = HalUsbMscInit();
    ConsoleWrite("msc: bringup=");
    ConsoleWrite(Rc == 0 ? "ok" : "fail");
    ConsoleWrite(" ready=");
    ConsoleWrite(HalUsbMscReady() ? "1" : "0");
    ConsoleWrite(" auto=");
    ConsoleWrite(HalUsbMscAutoEnabled() ? "1" : "0");
    ConsoleWrite(" (scan|claim|capacity|mount; auto=THEME msc=0|MSC.OFF)\n");
}

void ShellCommandsUsbRegister(void) {
    ConsoleRegister2("show", "xhci", "xHCI mode= + counters", CommandXhci);
    ConsoleRegister2("show", "input", "input counters (xhci or virtio)", CommandInputDiag);
    ConsoleRegister("msc",
                    "USB MSC: scan|claim|capacity|mount; boot auto (msc-7b)",
                    CommandMsc);
    ConsoleRegisterAliasLine("xhci", "show", "xhci");
    ConsoleRegisterAliasLine("input", "show", "input");
}
