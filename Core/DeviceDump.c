/*
 * DeviceDump.c — DeviceListDump（PR-DEV-3；Console 输出）
 */
#include "Device.h"
#include "Driver.h"
#include "Console.h"

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

void DeviceListDump(void) {
    int i;
    int N = DeviceCount();

    ConsoleWrite("=== Devices (");
    ConsoleWriteHex32((UINT32)N);
    ConsoleWrite(") ===\n");
    if (N <= 0) {
        ConsoleWrite("  (none)\n");
        return;
    }
    for (i = 0; i < N; i++) {
        DEVICE_NODE *Dev = DeviceGet(i);

        if (!Dev) {
            continue;
        }
        ConsoleWrite("  [");
        WriteHex2(Dev->PciBus);
        ConsoleWrite(":");
        WriteHex2(Dev->PciDev);
        ConsoleWrite(".");
        WriteHex2(Dev->PciFn & 0x7u);
        ConsoleWrite("] ");
        WriteHex4(Dev->Vendor);
        ConsoleWrite(":");
        WriteHex4(Dev->Device);
        ConsoleWrite("  ");
        ConsoleWrite(Dev->Name[0] ? Dev->Name : "pci");
        ConsoleWrite("  ");
        if (Dev->Bound && Dev->Driver && Dev->Driver->Name) {
            ConsoleWrite(Dev->Driver->Name);
        } else {
            ConsoleWrite("(unbound)");
        }
        ConsoleWrite("\n");
    }
}
