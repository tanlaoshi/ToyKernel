/*
 * IwlEapolM1Diag.c — M1 超时串口诊断（PR-F-iwl-1）
 */
#include "IwlEapolInternal.h"
#include "HalSerial.h"

void IwlEapolM1LogTimeout(const IWL_EAPOL_M1_CTX *C) {
    char Line[72];
    char Hex[12];
    int N = 0;
    const char *P = "wpa2=m1to n=";
    UINTN Si;

    while (*P) {
        Line[N++] = *P++;
    }
    HalSerialFormatHex(Hex, C->RxMpdu & 0xffu, 2);
    Line[N++] = Hex[2];
    Line[N++] = Hex[3];
    Line[N++] = ' ';
    Line[N++] = 'd';
    Line[N++] = '=';
    HalSerialFormatHex(Hex, C->RxData & 0xffu, 2);
    Line[N++] = Hex[2];
    Line[N++] = Hex[3];
    Line[N++] = ' ';
    Line[N++] = 'u';
    Line[N++] = '=';
    HalSerialFormatHex(Hex, C->RxUni & 0xffu, 2);
    Line[N++] = Hex[2];
    Line[N++] = Hex[3];
    Line[N++] = ' ';
    Line[N++] = 'e';
    Line[N++] = '=';
    HalSerialFormatHex(Hex, C->EapHit & 0xffu, 2);
    Line[N++] = Hex[2];
    Line[N++] = Hex[3];
    Line[N++] = ' ';
    Line[N++] = 'f';
    Line[N++] = '=';
    HalSerialFormatHex(Hex, C->FirstFc0, 2);
    Line[N++] = Hex[2];
    Line[N++] = Hex[3];
    HalSerialFormatHex(Hex, C->FirstFc1, 2);
    Line[N++] = Hex[2];
    Line[N++] = Hex[3];
    Line[N] = 0;
    IwlLogStage(Line);
    /* 第二行：单播头 + LLC 候选 + 末次 KeyInfo + 首 data DA */
    N = 0;
    P = "m1diag uf=";
    while (*P) {
        Line[N++] = *P++;
    }
    HalSerialFormatHex(Hex, C->UniFc0, 2);
    Line[N++] = Hex[2];
    Line[N++] = Hex[3];
    HalSerialFormatHex(Hex, C->UniFc1, 2);
    Line[N++] = Hex[2];
    Line[N++] = Hex[3];
    Line[N++] = ' ';
    Line[N++] = 's';
    Line[N++] = '=';
    for (Si = 0; Si < 8; Si++) {
        HalSerialFormatHex(Hex, C->UniSnap[Si], 2);
        Line[N++] = Hex[2];
        Line[N++] = Hex[3];
    }
    Line[N++] = ' ';
    Line[N++] = 'k';
    Line[N++] = '=';
    HalSerialFormatHex(Hex, (C->LastKi >> 8) & 0xffu, 2);
    Line[N++] = Hex[2];
    Line[N++] = Hex[3];
    HalSerialFormatHex(Hex, C->LastKi & 0xffu, 2);
    Line[N++] = Hex[2];
    Line[N++] = Hex[3];
    Line[N++] = ' ';
    Line[N++] = 'd';
    Line[N++] = 'a';
    Line[N++] = '=';
    for (Si = 0; Si < 6; Si++) {
        HalSerialFormatHex(Hex, C->DataDa[Si], 2);
        Line[N++] = Hex[2];
        Line[N++] = Hex[3];
    }
    Line[N] = 0;
    IwlLogStage(Line);
}
