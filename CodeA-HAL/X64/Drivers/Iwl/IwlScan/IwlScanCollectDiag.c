/*
 * IwlScanCollectDiag.c — 扫描失败诊断日志（PR-F-iwl-2）
 */
#include "IwlScanInternal.h"
#include "HalSerial.h"

void IwlScanCollectLogFail(const IWL_SCAN_COLLECT_CTX *C) {
    char Line[64];
    char Hex[12];
    char Want[9];
    int N = 0;
    UINT32 K;
    const char *P = "scan=fail n=";
    UINTN Wlen = 0;

    while (*P) {
        Line[N++] = *P++;
    }
    HalSerialFormatHex(Hex, (UINT32)gIwlScanCount, 4);
    Line[N++] = Hex[2];
    Line[N++] = Hex[3];
    Line[N++] = Hex[4];
    Line[N++] = Hex[5];
    Line[N++] = ' ';
    Line[N++] = C->GotAck ? 'A' : 'a';
    Line[N++] = C->GotDone ? 'D' : 'd';
    for (K = 0; K < C->Ncode && N < 50; K++) {
        Line[N++] = ' ';
        HalSerialFormatHex(Hex, C->Codes[K] & 0xffu, 2);
        Line[N++] = Hex[2];
        Line[N++] = Hex[3];
        if (C->Codes[K] & 0x100u) {
            Line[N++] = 'n';
        }
    }
    Line[N] = 0;
    IwlLogStage(Line);

    N = 0;
    P = "scan=miss b=";
    while (gIwlSsid[Wlen] && Wlen < 8u) {
        Wlen++;
    }
    IwlScanCopyVis(Want, (const UINT8 *)gIwlSsid, Wlen);
    while (*P && N < 48) {
        Line[N++] = *P++;
    }
    HalSerialFormatHex(Hex, C->BeaconN & 0xffu, 2);
    Line[N++] = Hex[2];
    Line[N++] = Hex[3];
    Line[N++] = ' ';
    Line[N++] = 'f';
    Line[N++] = 'c';
    Line[N++] = '=';
    HalSerialFormatHex(Hex, C->FirstFc, 2);
    Line[N++] = Hex[2];
    Line[N++] = Hex[3];
    Line[N++] = ' ';
    Line[N++] = 'h';
    Line[N++] = '=';
    P = C->Heard[0] ? C->Heard : "-";
    while (*P && N < 40) {
        Line[N++] = *P++;
    }
    Line[N++] = ' ';
    Line[N++] = 'w';
    Line[N++] = '=';
    P = Want[0] ? Want : "-";
    while (*P && N < 56) {
        Line[N++] = *P++;
    }
    Line[N] = 0;
    IwlLogStage(Line);
}
