/*
 * IwlEapolM1Start.c — M1 等待上下文 / EAPOL-Start（PR-F-iwl-1）
 */
#include "IwlEapolInternal.h"
#include "HalSerial.h"

void IwlEapolM1CtxInit(IWL_EAPOL_M1_CTX *C) {
    UINTN Si;

    C->RxMpdu = 0;
    C->RxData = 0;
    C->RxUni = 0;
    C->EapHit = 0;
    C->LastKi = 0;
    C->FirstFc0 = 0;
    C->FirstFc1 = 0;
    C->UniFc0 = 0;
    C->UniFc1 = 0;
    C->UniSnapOk = 0;
    C->DataDaOk = 0;
    C->M1At = 0;
    C->M1Fresh = 0;
    C->Got1 = 0;
    C->KeyInfo = 0;
    C->KeyDesc = 2;
    for (Si = 0; Si < 8; Si++) {
        C->UniSnap[Si] = 0;
    }
    for (Si = 0; Si < 6; Si++) {
        C->DataDa[Si] = 0;
    }
    C->Start[0] = 1;
    C->Start[1] = 1; /* EAPOL-Start */
    C->Start[2] = 0;
    C->Start[3] = 0;
}

void IwlEapolM1SendStart(IWL_EAPOL_M1_CTX *C) {
    char Hline[20];
    char Hex[12];
    int Hn = 0;
    const char *Hp = "hold=";
    UINT8 Hc = IwlRxHoldCount();

    while (*Hp) {
        Hline[Hn++] = *Hp++;
    }
    HalSerialFormatHex(Hex, Hc, 2);
    Hline[Hn++] = Hex[2];
    Hline[Hn++] = Hex[3];
    Hline[Hn] = 0;
    IwlLogStage(Hline);
    if (Hc) {
        IwlRxHoldFlush();
        IwlLogStage("hold=flush");
    }
    if (IwlSendEapol(C->Start, 4) == 0) {
        char Mline[40];
        int Mn = 0;
        const char *Mp = "eapol=start m=";
        UINTN Mi;

        while (*Mp) {
            Mline[Mn++] = *Mp++;
        }
        for (Mi = 0; Mi < 6; Mi++) {
            HalSerialFormatHex(Hex, gIwlMac[Mi], 2);
            Mline[Mn++] = Hex[2];
            Mline[Mn++] = Hex[3];
        }
        Mline[Mn++] = ' ';
        Mline[Mn++] = 'a';
        Mline[Mn++] = '=';
        HalSerialFormatHex(Hex, (gIwlAid >> 8) & 0xffu, 2);
        Mline[Mn++] = Hex[2];
        Mline[Mn++] = Hex[3];
        HalSerialFormatHex(Hex, gIwlAid & 0xffu, 2);
        Mline[Mn++] = Hex[2];
        Mline[Mn++] = Hex[3];
        Mline[Mn] = 0;
        IwlLogStage(Mline);
    }
}
