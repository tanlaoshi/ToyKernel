/* IwlEapolM1.c — 等 EAPOL msg1（PR-S-iwl-split-1） */
#include "IwlPrivate.h"
#include "HalSerial.h"

int IwlEapolWaitMsg1(UINT8 *Eapol, UINTN *EapLen, UINT8 *Anonce, UINT8 *Replay, UINT8 *KeyDescOut) {
    UINT32 i;
    int Got1 = 0;
    UINT16 KeyInfo = 0;
    UINT8 KeyDesc = 2;
    {
        UINT32 RxMpdu = 0;
        UINT32 RxData = 0;
        UINT32 RxUni = 0;
        UINT32 EapHit = 0;
        UINT16 LastKi = 0;
        UINT8 FirstFc0 = 0;
        UINT8 FirstFc1 = 0;
        UINT8 UniFc0 = 0;
        UINT8 UniFc1 = 0;
        UINT8 UniSnap[8];
        int UniSnapOk = 0;
        UINT8 DataDa[6];
        int DataDaOk = 0;
        UINT8 Start[4];
        UINTN si;
        UINT32 M1At = 0;
        int M1Fresh = 0;
        for (si = 0; si < 8; si++) {
            UniSnap[si] = 0;
        }
        for (si = 0; si < 6; si++) {
            DataDa[si] = 0;
        }
        Start[0] = 1;
        Start[1] = 1; /* EAPOL-Start */
        Start[2] = 0;
        Start[3] = 0;
        {
            char Hline[20];
            char Hex[12];
            int hn = 0;
            const char *Hp = "hold=";
            UINT8 Hc = IwlRxHoldCount();
            while (*Hp) {
                Hline[hn++] = *Hp++;
            }
            HalSerialFormatHex(Hex, Hc, 2);
            Hline[hn++] = Hex[2];
            Hline[hn++] = Hex[3];
            Hline[hn] = 0;
            IwlLogStage(Hline);
            if (Hc) {
                IwlRxHoldFlush();
                IwlLogStage("hold=flush");
            }
        }
        if (IwlSendEapol(Start, 4) == 0) {
            char Mline[40];
            char Hex[12];
            int mn = 0;
            const char *Mp = "eapol=start m=";
            UINTN mi;
            while (*Mp) {
                Mline[mn++] = *Mp++;
            }
            for (mi = 0; mi < 6; mi++) {
                HalSerialFormatHex(Hex, gIwlMac[mi], 2);
                Mline[mn++] = Hex[2];
                Mline[mn++] = Hex[3];
            }
            Mline[mn++] = ' ';
            Mline[mn++] = 'a';
            Mline[mn++] = '=';
            HalSerialFormatHex(Hex, (gIwlAid >> 8) & 0xffu, 2);
            Mline[mn++] = Hex[2];
            Mline[mn++] = Hex[3];
            HalSerialFormatHex(Hex, gIwlAid & 0xffu, 2);
            Mline[mn++] = Hex[2];
            Mline[mn++] = Hex[3];
            Mline[mn] = 0;
            IwlLogStage(Mline);
        }

        for (i = 0; i < 4000; i++) {
            IWL_RX_PKT *Pkt;
            UINTN Len;

            if (Got1 && i >= M1At + 100u) {
                break;
            }
            if ((i % 500u) == 499u) {
                (void)IwlSendEapol(Start, 4);
            }

            IwlRxPoll();
            {
                int Took = 0;
                for (;;) {
                    UINT8 Mutable[512];
                    UINTN FLen;
                    UINT8 *Ep;
                    UINTN EpLen;
                    UINTN k;
                    UINT16 Fc;
                    int ToUs;

                    if (Took >= 24) {
                        break;
                    }
                    Took++;
                    if (IwlRxTakeHeld(&Pkt, &Len)) {
                    } else if (!IwlRxTake(&Pkt, &Len)) {
                        break;
                    }
                if (Pkt->Hdr.Code != IWL_RX_MPDU_CMD || Len < 8) {
                    continue;
                }
                RxMpdu++;
                FLen = Len - sizeof(IWL_CMD_HDR) - 4;
                if (FLen > sizeof(Mutable)) {
                    FLen = sizeof(Mutable);
                }
                for (k = 0; k < FLen; k++) {
                    Mutable[k] = Pkt->Data[4 + k];
                }
                if (FLen < 24) {
                    continue;
                }
                Fc = (UINT16)Mutable[0] | ((UINT16)Mutable[1] << 8);
                if (FirstFc0 == 0 && FirstFc1 == 0) {
                    FirstFc0 = Mutable[0];
                    FirstFc1 = Mutable[1];
                }
                /* mgmt deauth/disassoc：AP 已踢掉则无需空等 M1 */
                if (((Fc >> 2) & 0x3u) == 0u) {
                    UINT8 Sub = (UINT8)((Fc >> 4) & 0x0fu);
                    if (Sub == 12u || Sub == 10u) {
                        IwlLogStage(Sub == 12u ? "rx=deauth" : "rx=disassoc");
                        Got1 = 0;
                        i = 4000;
                        break;
                    }
                    continue;
                }
                if (((Fc >> 2) & 0x3u) != 0x2u) {
                    continue;
                }
                RxData++;
                ToUs = IwlAddr1IsUs(Mutable);
                if (!DataDaOk) {
                    for (k = 0; k < 6; k++) {
                        DataDa[k] = Mutable[4 + k];
                    }
                    DataDaOk = 1;
                }
                if (ToUs) {
                    RxUni++;
                    if (!UniSnapOk) {
                        UINTN H = IwlDot11DataHdrLen(Fc);
                        UniFc0 = Mutable[0];
                        UniFc1 = Mutable[1];
                        for (k = 0; k < 8 && H + k < FLen; k++) {
                            UniSnap[k] = Mutable[H + k];
                        }
                        UniSnapOk = 1;
                    }
                }
                if (IwlFindEapol(Mutable, FLen, &Ep, &EpLen) && EpLen >= 99
                    && EpLen <= 256u) {
                    EapHit++;
                    if (Ep[4] == 2 || Ep[4] == 254) {
                        KeyInfo = IwlBe16(Ep + 5);
                        LastKi = KeyInfo;
                        /* M1：Ack、无 MIC；Accept Install=0 或偶发脏位 */
                        if ((KeyInfo & 0x0080u) != 0 && (KeyInfo & 0x0100u) == 0) {
                            int Newer = 0;
                            if (!Got1) {
                                Newer = 1;
                            } else {
                                for (k = 0; k < 8; k++) {
                                    if (Ep[9 + k] > Replay[k]) {
                                        Newer = 1;
                                        break;
                                    }
                                    if (Ep[9 + k] < Replay[k]) {
                                        break;
                                    }
                                }
                            }
                            if (Newer) {
                                IwlEapolCopyN(Eapol, Ep, EpLen);
                                *EapLen = EpLen;
                                IwlEapolCopyN(Anonce, Ep + 17, 32);
                                IwlEapolCopyN(Replay, Ep + 9, 8);
                                KeyDesc = Ep[4];
                                Got1 = 1;
                                M1At = i;
                                if (M1Fresh) {
                                    IwlLogStage("wpa2=m1new");
                                }
                                M1Fresh = 1;
                            }
                        }
                    } else {
                        LastKi = (UINT16)Ep[4];
                    }
                }
            }
            }
            if (!Got1 || i < M1At + 100u) {
                IwlStallMs(1);
            }
        }
        if (!Got1) {
            char Line[72];
            char Hex[12];
            int n = 0;
            const char *P = "wpa2=m1to n=";
            while (*P) {
                Line[n++] = *P++;
            }
            HalSerialFormatHex(Hex, RxMpdu & 0xffu, 2);
            Line[n++] = Hex[2];
            Line[n++] = Hex[3];
            Line[n++] = ' ';
            Line[n++] = 'd';
            Line[n++] = '=';
            HalSerialFormatHex(Hex, RxData & 0xffu, 2);
            Line[n++] = Hex[2];
            Line[n++] = Hex[3];
            Line[n++] = ' ';
            Line[n++] = 'u';
            Line[n++] = '=';
            HalSerialFormatHex(Hex, RxUni & 0xffu, 2);
            Line[n++] = Hex[2];
            Line[n++] = Hex[3];
            Line[n++] = ' ';
            Line[n++] = 'e';
            Line[n++] = '=';
            HalSerialFormatHex(Hex, EapHit & 0xffu, 2);
            Line[n++] = Hex[2];
            Line[n++] = Hex[3];
            Line[n++] = ' ';
            Line[n++] = 'f';
            Line[n++] = '=';
            HalSerialFormatHex(Hex, FirstFc0, 2);
            Line[n++] = Hex[2];
            Line[n++] = Hex[3];
            HalSerialFormatHex(Hex, FirstFc1, 2);
            Line[n++] = Hex[2];
            Line[n++] = Hex[3];
            Line[n] = 0;
            IwlLogStage(Line);
            /* 第二行：单播头 + LLC 候选 + 末次 KeyInfo + 首 data DA */
            n = 0;
            P = "m1diag uf=";
            while (*P) {
                Line[n++] = *P++;
            }
            HalSerialFormatHex(Hex, UniFc0, 2);
            Line[n++] = Hex[2];
            Line[n++] = Hex[3];
            HalSerialFormatHex(Hex, UniFc1, 2);
            Line[n++] = Hex[2];
            Line[n++] = Hex[3];
            Line[n++] = ' ';
            Line[n++] = 's';
            Line[n++] = '=';
            for (si = 0; si < 8; si++) {
                HalSerialFormatHex(Hex, UniSnap[si], 2);
                Line[n++] = Hex[2];
                Line[n++] = Hex[3];
            }
            Line[n++] = ' ';
            Line[n++] = 'k';
            Line[n++] = '=';
            HalSerialFormatHex(Hex, (LastKi >> 8) & 0xffu, 2);
            Line[n++] = Hex[2];
            Line[n++] = Hex[3];
            HalSerialFormatHex(Hex, LastKi & 0xffu, 2);
            Line[n++] = Hex[2];
            Line[n++] = Hex[3];
            Line[n++] = ' ';
            Line[n++] = 'd';
            Line[n++] = 'a';
            Line[n++] = '=';
            for (si = 0; si < 6; si++) {
                HalSerialFormatHex(Hex, DataDa[si], 2);
                Line[n++] = Hex[2];
                Line[n++] = Hex[3];
            }
            Line[n] = 0;
            IwlLogStage(Line);
            return 0;
        }
    }

    *KeyDescOut = KeyDesc;
    return Got1;
}
