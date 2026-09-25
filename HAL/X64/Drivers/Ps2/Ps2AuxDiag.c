/*
 * Ps2AuxDiag.c — Shell 诊断 / retry（PR-H-ps2-aux）
 */
#include "Ps2Private.h"
#include "HalSerial.h"

int Ps2AuxRetry(void) {
    UINT8 Ccb = 0x47;
    int i;

    Ps2DrainOb(64);
    Ps2CtrlCmd(0x20);
    if (Ps2DataRead(&Ccb)) {
        Ccb &= (UINT8)~(1u << 5);
        Ccb &= (UINT8)~(1u << 4);
        Ccb &= (UINT8)~(1u << 6);
        Ps2CtrlCmd(0x60);
        Ps2DataWrite(Ccb);
    }
    Ps2CtrlCmd(0xA8);
    for (i = 0; i < 200000; i++) {
        HalCpuRelax();
    }
    if (!Ps2AuxInitDevice()) {
        gPs2AuxReady = 0;
        return 0;
    }
    gPs2AuxReady = 1;
    return 1;
}

void Ps2DiagFormat(char *Buf, int Max) {
    int n = 0;
    char Hex[12];
    const char *P;
    UINT32 Pkts;
    UINT32 Bytes;

    if (!Buf || Max < 12) {
        return;
    }
    Pkts = Ps2AuxPktCount();
    Bytes = Ps2AuxByteCount();
    Buf[0] = 0;
    P = "kbd=";
    while (*P && n < Max - 1) {
        Buf[n++] = *P++;
    }
    Buf[n++] = gPs2Ready ? '1' : '0';
    P = " aux=";
    while (*P && n < Max - 1) {
        Buf[n++] = *P++;
    }
    Buf[n++] = gPs2AuxReady ? '1' : '0';
    P = " fail=";
    while (*P && n < Max - 1) {
        Buf[n++] = *P++;
    }
    P = gPs2Fail ? gPs2Fail : "?";
    while (*P && n < Max - 1) {
        Buf[n++] = *P++;
    }
    P = " rx=";
    while (*P && n < Max - 1) {
        Buf[n++] = *P++;
    }
    HalSerialFormatHex(Hex, gPs2LastRx, 2);
    Buf[n++] = Hex[2];
    Buf[n++] = Hex[3];
    P = " id=";
    while (*P && n < Max - 1) {
        Buf[n++] = *P++;
    }
    HalSerialFormatHex(Hex, gPs2DevId, 2);
    Buf[n++] = Hex[2];
    Buf[n++] = Hex[3];
    P = " pkts=";
    while (*P && n < Max - 1) {
        Buf[n++] = *P++;
    }
    HalSerialFormatHex(Hex, Pkts, 4);
    {
        int i;
        for (i = 2; i < 6 && n < Max - 1; i++) {
            Buf[n++] = Hex[i];
        }
    }
    P = " bytes=";
    while (*P && n < Max - 1) {
        Buf[n++] = *P++;
    }
    HalSerialFormatHex(Hex, Bytes, 4);
    {
        int i;
        for (i = 2; i < 6 && n < Max - 1; i++) {
            Buf[n++] = Hex[i];
        }
    }
    Buf[n] = 0;
}
