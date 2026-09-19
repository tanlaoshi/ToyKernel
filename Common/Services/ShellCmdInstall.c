/*
 * ShellCmdInstall.c — install 命令（PR-S-shellfs-split-1）
 *
 * 从 ShellCommandsFs.c 迁出；只搬家、不改逻辑。
 */
#include "ShellPrivate.h"
#include "Console.h"
#include "FileSystem.h"
#include "Install.h"
#include "Block.h"

static void InstallWriteDec(UINT32 V) {
    char Buf[12];
    int i = 0;
    int j;
    char Tmp[12];

    if (V == 0) {
        ConsoleWrite("0");
        return;
    }
    while (V > 0 && i < (int)sizeof(Tmp)) {
        Tmp[i++] = (char)('0' + (V % 10u));
        V /= 10u;
    }
    j = 0;
    while (i > 0) {
        Buf[j++] = Tmp[--i];
    }
    Buf[j] = 0;
    ConsoleWrite(Buf);
}

static void InstallPrintDisk(UINT32 Drive, int Ready) {
    ConsoleWrite("  drive ");
    InstallWriteDec(Drive);
    ConsoleWrite(Ready ? " ready\n" : " empty\n");
}

static int ParseU32(const char *S, UINT32 *Out) {
    UINT32 V = 0;
    const char *P = S;

    if (!S || !Out) {
        return 0;
    }
    if (P[0] == '0' && (P[1] == 'x' || P[1] == 'X')) {
        P += 2;
        if (!*P) {
            return 0;
        }
        while (*P) {
            char C = *P++;
            UINT32 D;
            if (C >= '0' && C <= '9') {
                D = (UINT32)(C - '0');
            } else if (C >= 'a' && C <= 'f') {
                D = (UINT32)(C - 'a' + 10);
            } else if (C >= 'A' && C <= 'F') {
                D = (UINT32)(C - 'A' + 10);
            } else {
                return 0;
            }
            V = (V << 4) | D;
        }
        *Out = V;
        return 1;
    }
    if (*P < '0' || *P > '9') {
        return 0;
    }
    while (*P >= '0' && *P <= '9') {
        V = V * 10u + (UINT32)(*P - '0');
        P++;
    }
    if (*P != 0) {
        return 0;
    }
    *Out = V;
    return 1;
}

/* PR-FS-inst-1：install disks | install <drive> [--yes] [--mib N] [--force] */
static void CommandInstall(int Argc, char **Argv) {
    UINT32 Drive = 0;
    UINT32 Mib = 512;
    int Yes = 0;
    int Force = 0;
    int i;
    UINT64 Sectors;
    int Rc;
    int VolN;
    int v;

    if (Argc >= 2 && Argv[1][0] == 'd') {
        ConsoleWrite("install disks:\n");
        InstallListDisks(InstallPrintDisk);
        ConsoleWrite("hint: need blank raw disk (TOY_DISK=ahci TOY_INSTALL_DISK=1 → drive 2)\n");
        return;
    }
    if (Argc < 2) {
        ConsoleWrite("usage: install disks\n");
        ConsoleWrite("       install <drive> [--yes] [--mib N] [--force]\n");
        ConsoleWrite("  drive: decimal 2 or hex 0x2. Wipes disk. Smoke: AHCI drive 2.\n");
        return;
    }
    if (!ParseU32(Argv[1], &Drive)) {
        ConsoleWrite("install: bad drive (use 2 or 0x2)\n");
        return;
    }
    for (i = 2; i < Argc; i++) {
        if (Argv[i][0] == '-' && Argv[i][1] == '-' && Argv[i][2] == 'y') {
            Yes = 1;
        } else if (Argv[i][0] == '-' && Argv[i][1] == '-' && Argv[i][2] == 'f') {
            Force = 1;
        } else if (Argv[i][0] == '-' && Argv[i][1] == '-' && Argv[i][2] == 'm') {
            if (i + 1 < Argc) {
                if (!ParseU32(Argv[i + 1], &Mib)) {
                    ConsoleWrite("install: bad --mib\n");
                    return;
                }
                i++;
            }
        }
    }
    if (!BlockDriveReady(Drive)) {
        ConsoleWrite("install: drive ");
        InstallWriteDec(Drive);
        ConsoleWrite(" empty — start with TOY_DISK=ahci TOY_INSTALL_DISK=1\n");
        return;
    }
    /* 拒绝误装到当前已挂载的 ESP/TOYOS（vvfat），除非 --force */
    VolN = FileSystemVolCount();
    for (v = 0; v < VolN; v++) {
        char Name[16];
        UINT32 Dr = 0;
        UINT32 Lba = 0;
        int Ro = 0;
        if (FileSystemVolInfo(v, Name, (int)sizeof(Name), &Dr, &Lba, &Ro) != 0) {
            continue;
        }
        if (Dr == Drive && !Force) {
            ConsoleWrite("install: drive ");
            InstallWriteDec(Drive);
            ConsoleWrite(" hosts volume ");
            ConsoleWrite(Name);
            ConsoleWrite(" — refuse (use blank install disk, or --force)\n");
            return;
        }
    }
    if (Mib < 512) {
        ConsoleWrite("install: --mib need >=512\n");
        return;
    }
    Sectors = (UINT64)Mib * 2048ull;
    Rc = InstallToDrive(Drive, Sectors, 256, Yes);
    if (Rc != 0) {
        ConsoleWrite("install: failed\n");
    }
}

void ShellCmdInstallRegister(void) {
    ConsoleRegister("install", "install GPT+ESP+TOYOS to Block (PR-FS-inst-1)", CommandInstall);
}
