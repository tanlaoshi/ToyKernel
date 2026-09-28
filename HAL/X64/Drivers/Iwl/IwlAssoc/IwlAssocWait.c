/*
 * IwlAssocWait.c — Auth/Assoc 收包等待（PR-S-iwl-split-4）
 */
#include "IwlAssocInternal.h"
#include "HalSerial.h"

int IwlAssocWaitAuth(UINT32 *RxN, UINT8 *FirstFc) {
    UINTN i;
    IWL_RX_PKT *Pkt;
    UINTN Len;
    int GotAuth = 0;
    UINT32 BeaconN = 0;

    for (i = 0; i < 1500 && !GotAuth; i++) {
        IwlRxPoll();
        while (IwlAssocTake(&Pkt, &Len)) {
            const UINT8 *Dot;
            UINTN PayLen;
            UINT8 Sub;
            if (Pkt->Hdr.Code != IWL_RX_MPDU_CMD || Len < 12) {
                continue;
            }
            Dot = Pkt->Data + 4;
            PayLen = Len - sizeof(IWL_CMD_HDR) - 4;
            (*RxN)++;
            if (PayLen < 24) {
                continue;
            }
            if (*FirstFc == 0) {
                *FirstFc = Dot[0];
            }
            if ((Dot[0] & 0x0Cu) != 0) {
                if (((Dot[0] >> 2) & 0x3u) == 0x2u) {
                    IwlRxHoldMpdu(Pkt, Len);
                }
                continue;
            }
            Sub = (UINT8)((Dot[0] >> 4) & 0x0Fu);
            if (Sub == 8u || Sub == 5u) {
                BeaconN++;
            }
            if (Sub == 0xBu) {
                GotAuth = 1;
            }
        }
        IwlStallMs(1);
    }
    if (!GotAuth) {
        char Line[32];
        char Hex[12];
        int n = 0;
        const char *P = "auth=to n=";
        while (*P) {
            Line[n++] = *P++;
        }
        HalSerialFormatHex(Hex, (*RxN) & 0xffu, 2);
        Line[n++] = Hex[2];
        Line[n++] = Hex[3];
        Line[n++] = ' ';
        Line[n++] = 'f';
        Line[n++] = '=';
        HalSerialFormatHex(Hex, *FirstFc, 2);
        Line[n++] = Hex[2];
        Line[n++] = Hex[3];
        Line[n++] = ' ';
        Line[n++] = 'b';
        Line[n++] = '=';
        HalSerialFormatHex(Hex, BeaconN & 0xffu, 2);
        Line[n++] = Hex[2];
        Line[n++] = Hex[3];
        Line[n] = 0;
        IwlLogStage(Line);
        if (*RxN == 0) {
            (void)IwlCmdqUnwedge();
            (void)IwlPhyCtxtTune(gIwlTarget.Chan);
        }
        return 0;
    }
    return 1;
}

int IwlAssocWaitResp(UINT32 *RxN, UINT8 *FirstFc, int *GotAssoc) {
    UINTN i;
    IWL_RX_PKT *Pkt;
    UINTN Len;
    UINT32 BeaconN = 0;
    UINT32 RxLate = 0;

    for (i = 0; i < 2500 && !*GotAssoc; i++) {
        int Took = 0;

        if (i == 2000u) {
            RxLate = *RxN;
        }
        IwlRxPoll();
        while (Took < 32 && IwlAssocTake(&Pkt, &Len)) {
            const UINT8 *Dot;
            UINTN PayLen;
            UINT8 Sub;

            Took++;
            if (Pkt->Hdr.Code != IWL_RX_MPDU_CMD || Len < 12) {
                continue;
            }
            Dot = Pkt->Data + 4;
            PayLen = Len - sizeof(IWL_CMD_HDR) - 4;
            (*RxN)++;
            if (PayLen < 24) {
                continue;
            }
            if (*FirstFc == 0) {
                *FirstFc = Dot[0];
            }
            if ((Dot[0] & 0x0Cu) != 0) {
                if (((Dot[0] >> 2) & 0x3u) == 0x2u) {
                    IwlRxHoldMpdu(Pkt, Len);
                }
                continue;
            }
            Sub = (UINT8)((Dot[0] >> 4) & 0x0Fu);
            if (Sub == 8u || Sub == 5u) {
                BeaconN++;
            }
            if (Sub == 0x1u && PayLen >= 30) {
                UINT16 St = (UINT16)Dot[26] | ((UINT16)Dot[27] << 8);
                if (St == 0) {
                    *GotAssoc = 1;
                    if (PayLen >= 32) {
                        gIwlAid = (UINT16)Dot[28] | ((UINT16)Dot[29] << 8);
                        gIwlAid &= 0x3fffu;
                    }
                } else {
                    char Line[24];
                    char Hex[12];
                    int n = 0;
                    const char *Ps = "assoc=st";
                    while (*Ps) {
                        Line[n++] = *Ps++;
                    }
                    HalSerialFormatHex(Hex, St, 4);
                    Line[n++] = Hex[2];
                    Line[n++] = Hex[3];
                    Line[n++] = Hex[4];
                    Line[n++] = Hex[5];
                    Line[n] = 0;
                    IwlLogStage(Line);
                }
            }
        }
        if (i == 399u) {
            char Line[28];
            char Hex[12];
            int n = 0;
            const char *Pr = "assoc=rn n=";

            while (*Pr) {
                Line[n++] = *Pr++;
            }
            HalSerialFormatHex(Hex, (*RxN) & 0xffu, 2);
            Line[n++] = Hex[2];
            Line[n++] = Hex[3];
            Line[n++] = ' ';
            Line[n++] = 'f';
            Line[n++] = '=';
            HalSerialFormatHex(Hex, *FirstFc, 2);
            Line[n++] = Hex[2];
            Line[n++] = Hex[3];
            Line[n] = 0;
            IwlLogStage(Line);
        }
        IwlStallMs(1);
    }
    if (!*GotAssoc) {
        char Line[32];
        char Hex[12];
        int n = 0;
        const char *P = "assoc=to n=";
        while (*P) {
            Line[n++] = *P++;
        }
        HalSerialFormatHex(Hex, (*RxN) & 0xffu, 2);
        Line[n++] = Hex[2];
        Line[n++] = Hex[3];
        Line[n++] = ' ';
        Line[n++] = 'f';
        Line[n++] = '=';
        HalSerialFormatHex(Hex, *FirstFc, 2);
        Line[n++] = Hex[2];
        Line[n++] = Hex[3];
        Line[n++] = ' ';
        Line[n++] = 'b';
        Line[n++] = '=';
        HalSerialFormatHex(Hex, BeaconN & 0xffu, 2);
        Line[n++] = Hex[2];
        Line[n++] = Hex[3];
        Line[n] = 0;
        IwlLogStage(Line);
        if (*RxN == 0 || *RxN == RxLate) {
            IwlLogStage("assoc=rxstall");
            (void)IwlCmdqUnwedge();
            (void)IwlPhyCtxtTune(gIwlTarget.Chan);
        }
        return 0;
    }
    return 1;
}
