/*
 * DeviceDump.c — lsdev 可读行（PR-DEV-lsdev）
 * 默认：位 + FriendlyName + (bound, driver)|(unbound)
 * -v 追加 VID:DID / class / BAR0 / IRQ；-b / -u / -c 过滤；文末 Totals。
 */
#include "Device.h"
#include "Driver.h"
#include "Console.h"

typedef struct {
    int Verbose;
    int BoundOnly;
    int UnboundOnly;
    int HasClass;
    int ClassOk;
    UINT8 Class;
    int TreeMode;
    char NameKey[32];
} LIST_OPTS;

static char HexDigit(UINT32 V) {
    V &= 0xFu;
    return (char)(V < 10 ? ('0' + V) : ('a' + V - 10));
}

static void WriteHex2(UINT32 V) {
    char Buf[3];

    Buf[0] = HexDigit(V >> 4);
    Buf[1] = HexDigit(V);
    Buf[2] = 0;
    ConsoleWrite(Buf);
}

static void WriteHex4(UINT32 V) {
    char Buf[5];

    Buf[0] = HexDigit(V >> 12);
    Buf[1] = HexDigit(V >> 8);
    Buf[2] = HexDigit(V >> 4);
    Buf[3] = HexDigit(V);
    Buf[4] = 0;
    ConsoleWrite(Buf);
}

static void WriteDec(UINT32 V) {
    char Buf[12];
    int N = 0, i;

    if (V == 0) { ConsoleWrite("0"); return; }
    while (V > 0 && N < 11) {
        Buf[N++] = (char)('0' + (V % 10));
        V /= 10;
    }
    for (i = N - 1; i >= 0; i--) {
        char One[2];
        One[0] = Buf[i]; One[1] = 0; ConsoleWrite(One);
    }
}

static int StrEq(const char *A, const char *B) {
    if (!A || !B) {
        return 0;
    }
    while (*A && *A == *B) {
        A++;
        B++;
    }
    return *A == *B;
}

static const char *TitleOf(const DEVICE_NODE *Dev) {
    if (Dev->FriendlyName[0]) {
        return Dev->FriendlyName;
    }
    if (Dev->Name[0]) {
        return Dev->Name;
    }
    return "pci";
}

static int ParseClassByte(const char *S, UINT8 *Out) {
    UINT32 V = 0;
    int Digits = 0;

    if (S[0] == '0' && (S[1] == 'x' || S[1] == 'X')) {
        S += 2;
    }
    if (*S == 0) {
        return 0;
    }
    while (*S) {
        char C = *S++;
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
        Digits++;
        if (Digits > 2) {
            return 0;
        }
    }
    *Out = (UINT8)V;
    return 1;
}

static void CopyKey(char *Dst, const char *Src) {
    int i;

    for (i = 0; i < 31 && Src[i]; i++) {
        Dst[i] = Src[i];
    }
    Dst[i] = 0;
}

static int Want(const DEVICE_NODE *Dev, const LIST_OPTS *Opt) {
    if (Opt->BoundOnly && !Dev->Bound) {
        return 0;
    }
    if (Opt->UnboundOnly && Dev->Bound) {
        return 0;
    }
    if (Opt->HasClass) {
        int Ok = 0;

        if (Opt->ClassOk && Dev->Class == Opt->Class) {
            Ok = 1;
        }
        if (Opt->NameKey[0] && StrEq(Dev->Name, Opt->NameKey)) {
            Ok = 1;
        }
        if (!Ok) {
            return 0;
        }
    }
    return 1;
}

static void WriteVerbose(const DEVICE_NODE *Dev) {
    ConsoleWrite("      ");
    WriteHex4(Dev->Vendor);
    ConsoleWrite(":");
    WriteHex4(Dev->Device);
    ConsoleWrite("  class=");
    WriteHex2(Dev->Class);
    ConsoleWrite("/");
    WriteHex2(Dev->Subclass);
    ConsoleWrite("/");
    WriteHex2(Dev->ProgIf);
    ConsoleWrite("  BAR0=");
    ConsoleWriteHex64(Dev->Bar[0]);
    ConsoleWrite("  IRQ=");
    WriteHex2(Dev->Irq);
    ConsoleWrite("\n");
}

static void Dump(const LIST_OPTS *Opt) {
    int i;
    int Total = 0;
    int Bound = 0;
    int N;

    DeviceSyncBound();
    N = DeviceCount();
    ConsoleWrite("=== Devices ===\n");
    for (i = 0; i < N; i++) {
        DEVICE_NODE *Dev = DeviceGet(i);
        const char *Title;
        int Pad;
        int Len;

        if (!Dev || !Want(Dev, Opt)) {
            continue;
        }
        ConsoleWrite("  [");
        WriteHex2(Dev->PciBus);
        ConsoleWrite(":");
        WriteHex2(Dev->PciDev);
        ConsoleWrite(".");
        WriteHex2(Dev->PciFn & 0x7u);
        ConsoleWrite("] ");
        Title = TitleOf(Dev);
        ConsoleWrite(Title);
        Len = 0;
        while (Title[Len]) {
            Len++;
        }
        for (Pad = Len; Pad < 36; Pad++) {
            ConsoleWrite(" ");
        }
        if (Dev->Bound && Dev->Driver && Dev->Driver->Name) {
            ConsoleWrite(" (bound, ");
            ConsoleWrite(Dev->Driver->Name);
            ConsoleWrite(")");
            Bound++;
        } else {
            ConsoleWrite(" (unbound)");
        }
        ConsoleWrite("\n");
        if (Opt->Verbose) {
            WriteVerbose(Dev);
        }
        Total++;
    }
    if (Total == 0) {
        ConsoleWrite("  (none)\n");
    }
    ConsoleWrite("=== Totals ===\n");
    ConsoleWrite("  Total: ");
    WriteDec((UINT32)Total);
    ConsoleWrite("  Bound: ");
    WriteDec((UINT32)Bound);
    ConsoleWrite("\n");
}

void DeviceListDump(void) {
    LIST_OPTS Opt;

    Opt.Verbose = 0;
    Opt.BoundOnly = 0;
    Opt.UnboundOnly = 0;
    Opt.HasClass = 0;
    Opt.ClassOk = 0;
    Opt.Class = 0;
    Opt.TreeMode = 0;
    Opt.NameKey[0] = 0;
    Dump(&Opt);
}

int DeviceListDumpArgs(int Argc, char **Argv) {
    LIST_OPTS Opt;
    int i;

    Opt.Verbose = 0;
    Opt.BoundOnly = 0;
    Opt.UnboundOnly = 0;
    Opt.HasClass = 0;
    Opt.ClassOk = 0;
    Opt.Class = 0;
    Opt.TreeMode = 0;
    Opt.NameKey[0] = 0;

    for (i = 0; i < Argc; i++) {
        const char *A = Argv[i];

        if (!A || StrEq(A, "devices")) {
            continue;
        }
        if (StrEq(A, "-v")) {
            Opt.Verbose = 1;
            continue;
        }
        if (StrEq(A, "-t")) {
            Opt.TreeMode = 1;
            continue;
        }
        if (StrEq(A, "-b")) {
            Opt.BoundOnly = 1;
            continue;
        }
        if (StrEq(A, "-u")) {
            Opt.UnboundOnly = 1;
            continue;
        }
        if (StrEq(A, "-c")) {
            const char *Key;

            if (i + 1 >= Argc || !Argv[i + 1]) {
                ConsoleWrite("usage: list devices [-v] [-t] [-b|-u] [-c class]\n");
                return -1;
            }
            Key = Argv[++i];
            Opt.HasClass = 1;
            CopyKey(Opt.NameKey, Key);
            Opt.ClassOk = ParseClassByte(Key, &Opt.Class);
            continue;
        }
        ConsoleWrite("usage: list devices [-v] [-t] [-b|-u] [-c class]\n");
        return -1;
    }
    if (Opt.BoundOnly && Opt.UnboundOnly) {
        ConsoleWrite("usage: list devices [-v] [-t] [-b|-u] [-c class]\n");
        return -1;
    }
    if (Opt.TreeMode) {
        DeviceListDumpTree(Opt.Verbose);
        return 0;
    }
    Dump(&Opt);
    return 0;
}
