/*
 * DeviceTreeDump.c — PR-DEV-tree-lsdev：lsdev -t 树状打印。
 *
 * 根 = Parent==NULL；DeviceGetChildren 扫表取子。缩进 2 空格 × 深度。
 * 深度 ≥16 停止并串口警告（DebugWrite）。有 FriendlyName 用它，
 * 否则 Name + [bus:dev.fn]。-v 时每节点后追加 VID:DID/class/BAR0/IRQ 行。
 */
#include "Device.h"
#include "Console.h"
#include "Debug.h"

static char HexDigit(UINT32 V) {
    V &= 0xFu;
    return (char)(V < 10 ? ('0' + V) : ('a' + V - 10));
}

static void WriteHex2(UINT32 V) {
    char B[3];
    B[0] = HexDigit(V >> 4);
    B[1] = HexDigit(V);
    B[2] = 0;
    ConsoleWrite(B);
}

static void WriteHex4(UINT32 V) {
    char B[5];
    B[0] = HexDigit(V >> 12);
    B[1] = HexDigit(V >> 8);
    B[2] = HexDigit(V >> 4);
    B[3] = HexDigit(V);
    B[4] = 0;
    ConsoleWrite(B);
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

static void PrintNode(DEVICE_NODE *Dev, int Depth, int Verbose) {
    int i;

    for (i = 0; i < Depth * 2; i++) {
        ConsoleWrite(" ");
    }
    ConsoleWrite("[");
    WriteHex2(Dev->PciBus);
    ConsoleWrite(":");
    WriteHex2(Dev->PciDev);
    ConsoleWrite(".");
    WriteHex2(Dev->PciFn & 0x7u);
    ConsoleWrite("] ");
    ConsoleWrite(TitleOf(Dev));
    ConsoleWrite("\n");
    if (Verbose) {
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
}

static void PrintSubtree(DEVICE_NODE *Parent, int Depth, int Verbose) {
    DEVICE_NODE *Children[32];
    int N;
    int i;

    if (Depth >= 16) {
        DebugWrite("lsdev -t: depth >=16, stop\n");
        return;
    }
    N = DeviceGetChildren(Parent, Children, 32);
    for (i = 0; i < N; i++) {
        PrintNode(Children[i], Depth, Verbose);
        PrintSubtree(Children[i], Depth + 1, Verbose);
    }
}

void DeviceListDumpTree(int Verbose) {
    int i;
    int N = DeviceCount();

    ConsoleWrite("=== Devices (tree) ===\n");
    for (i = 0; i < N; i++) {
        DEVICE_NODE *Dev = DeviceGet(i);

        if (!Dev || Dev->Parent) {
            continue;
        }
        PrintNode(Dev, 0, Verbose);
        PrintSubtree(Dev, 1, Verbose);
    }
}
