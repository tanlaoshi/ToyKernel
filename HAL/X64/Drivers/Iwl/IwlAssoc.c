/*
 * IwlAssoc.c — open/WPA2 关联（auth → assoc）（PR-N-wifi-2）
 */
#include "IwlPrivate.h"
#include "HalSerial.h"

static void IwlCopyN(UINT8 *D, const UINT8 *S, UINTN N) {
    UINTN i;
    for (i = 0; i < N; i++) {
        D[i] = S[i];
    }
}

static void IwlBuildMgmt(UINT8 *Out, UINTN *OutLen, UINT8 Subtype,
                         const UINT8 *Payload, UINTN PayLen) {
    UINTN i;
    Out[0] = (UINT8)(0x00 | (Subtype << 4));
    Out[1] = 0x00;
    Out[2] = 0;
    Out[3] = 0;
    IwlCopyN(Out + 4, gIwlTarget.Bssid, 6);
    IwlCopyN(Out + 10, gIwlMac, 6);
    IwlCopyN(Out + 16, gIwlTarget.Bssid, 6);
    Out[22] = 0;
    Out[23] = 0;
    for (i = 0; i < PayLen; i++) {
        Out[24 + i] = Payload[i];
    }
    *OutLen = 24 + PayLen;
}

/* 刀 #102：live + Hold 一起抽（phy Sync 会把 MPDU 塞进 Hold） */
static int IwlAssocTake(IWL_RX_PKT **Pkt, UINTN *Len) {
    if (IwlRxTakeHeld(Pkt, Len)) {
        return 1;
    }
    return IwlRxTake(Pkt, Len);
}

int IwlAssocRun(void) {
    UINT8 Frame[160];
    UINT8 Pay[120];
    UINTN Flen = 0;
    UINTN i;
    IWL_RX_PKT *Pkt;
    UINTN Len;
    int GotAuth = 0;
    int GotAssoc = 0;
    int Attempt;
    UINT32 RxN = 0;
    UINT8 FirstFc = 0;

    if (!gIwlSsidOk) {
        return 0;
    }
    IwlCopyN(gIwlBssid, gIwlTarget.Bssid, 6);
    gIwlAssociated = 0;
    gIwlAid = 0;

    (void)IwlPhyCtxtTune(gIwlTarget.Chan);

    /*
     * 刀 #113：#112 偶发 auth=ok 后 assoc 仅 beacon，重试 auth 变 n=00。
     * 一旦 auth 成功就记住；重试只狂发 Assoc，不再重做 Auth。
     */
    {
        int EverAuth = 0;
        int Prepped = 0;

        for (Attempt = 0; Attempt < 4 && !GotAssoc; Attempt++) {
            GotAuth = 0;
            RxN = 0;
            FirstFc = 0;

            if (!EverAuth) {
                Pay[0] = 0;
                Pay[1] = 0;
                Pay[2] = 1;
                Pay[3] = 0;
                Pay[4] = 0;
                Pay[5] = 0;
                IwlBuildMgmt(Frame, &Flen, 0xB, Pay, 6);
                if (IwlSendFrameRaw(Frame, Flen) != 0) {
                    return 0;
                }
                (void)IwlSendFrameRaw(Frame, Flen);

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
                        RxN++;
                        if (PayLen < 24) {
                            continue;
                        }
                        if (FirstFc == 0) {
                            FirstFc = Dot[0];
                        }
                        if ((Dot[0] & 0x0Cu) != 0) {
                            /* #132：data 可能是抢跑 M1 */
                            if (((Dot[0] >> 2) & 0x3u) == 0x2u) {
                                IwlRxHoldMpdu(Pkt, Len);
                            }
                            continue;
                        }
                        Sub = (UINT8)((Dot[0] >> 4) & 0x0Fu);
                        if (Sub == 0xBu) {
                            GotAuth = 1;
                        }
                    }
                    IwlStallMs(1);
                }
                if (!GotAuth) {
                    char Line[28];
                    char Hex[12];
                    int n = 0;
                    const char *P = "auth=to n=";
                    while (*P) {
                        Line[n++] = *P++;
                    }
                    HalSerialFormatHex(Hex, RxN & 0xffu, 2);
                    Line[n++] = Hex[2];
                    Line[n++] = Hex[3];
                    Line[n++] = ' ';
                    Line[n++] = 'f';
                    Line[n++] = '=';
                    HalSerialFormatHex(Hex, FirstFc, 2);
                    Line[n++] = Hex[2];
                    Line[n++] = Hex[3];
                    Line[n] = 0;
                    IwlLogStage(Line);
                    (void)IwlPhyCtxtTune(gIwlTarget.Chan);
                    continue;
                } else {
                    IwlLogStage("auth=ok");
                    EverAuth = 1;
                }
            } else {
                IwlLogVerb("auth=keep");
            }

            /* 刀 #132：#131 前置 Prep 打挂 auth；改到 auth 后、Assoc 前 */
            if (!Prepped) {
                if (!IwlMacCtxtPrep()) {
                    IwlLogStage("prep=soft");
                }
                Prepped = 1;
            }

            Pay[0] = (UINT8)(gIwlTarget.Caps);
            Pay[1] = (UINT8)(gIwlTarget.Caps >> 8);
            Pay[2] = 0x0A;
            Pay[3] = 0x00;
            Pay[4] = 0;
            Pay[5] = gIwlTarget.SsidLen;
            IwlCopyN(Pay + 6, gIwlTarget.Ssid, gIwlTarget.SsidLen);
            {
                UINTN P = 6 + gIwlTarget.SsidLen;
                UINT8 RatesLen = gIwlTarget.RatesLen;
                UINT8 ExtLen = gIwlTarget.ExtRatesLen;
                static const UINT8 DefRates[] = {
                    0x82, 0x84, 0x8b, 0x96, 0x0c, 0x12, 0x18, 0x24
                };
                static const UINT8 DefExt[] = {
                    0x30, 0x48, 0x60, 0x6c
                };

                if (RatesLen == 0) {
                    RatesLen = (UINT8)sizeof(DefRates);
                    IwlCopyN(Pay + P + 2, DefRates, RatesLen);
                } else {
                    IwlCopyN(Pay + P + 2, gIwlTarget.Rates, RatesLen);
                }
                Pay[P] = 1;
                Pay[P + 1] = RatesLen;
                P += 2 + RatesLen;

                if (ExtLen == 0 && gIwlTarget.RatesLen == 0) {
                    ExtLen = (UINT8)sizeof(DefExt);
                    IwlCopyN(Pay + P + 2, DefExt, ExtLen);
                    Pay[P] = 50;
                    Pay[P + 1] = ExtLen;
                    P += 2 + ExtLen;
                } else if (ExtLen > 0) {
                    IwlCopyN(Pay + P + 2, gIwlTarget.ExtRates, ExtLen);
                    Pay[P] = 50;
                    Pay[P + 1] = ExtLen;
                    P += 2 + ExtLen;
                }

                if (gIwlTarget.HasRsn && gIwlPsk[0]) {
                    static const UINT8 Rsn[] = {
                        0x30, 0x14, 0x01, 0x00, 0x00, 0x0f, 0xac, 0x04,
                        0x01, 0x00, 0x00, 0x0f, 0xac, 0x04, 0x01, 0x00,
                        0x00, 0x0f, 0xac, 0x02, 0x00, 0x00
                    };
                    UINTN r;
                    for (r = 0; r < sizeof(Rsn) && P + r < sizeof(Pay); r++) {
                        Pay[P + r] = Rsn[r];
                    }
                    P += sizeof(Rsn);
                }
                IwlBuildMgmt(Frame, &Flen, 0x0, Pay, P);
            }
            if (IwlSendFrameRaw(Frame, Flen) != 0) {
                return 0;
            }
            (void)IwlSendFrameRaw(Frame, Flen);
            RxN = 0;
            FirstFc = 0;
            for (i = 0; i < 2500 && !GotAssoc; i++) {
                if ((i % 400u) == 399u) {
                    (void)IwlSendFrameRaw(Frame, Flen);
                }
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
                    RxN++;
                    if (PayLen < 24) {
                        continue;
                    }
                    if (FirstFc == 0) {
                        FirstFc = Dot[0];
                    }
                    if ((Dot[0] & 0x0Cu) != 0) {
                        if (((Dot[0] >> 2) & 0x3u) == 0x2u) {
                            IwlRxHoldMpdu(Pkt, Len);
                        }
                        continue;
                    }
                    Sub = (UINT8)((Dot[0] >> 4) & 0x0Fu);
                    if (Sub == 0x1u && PayLen >= 30) {
                        UINT16 St = (UINT16)Dot[26] | ((UINT16)Dot[27] << 8);
                        if (St == 0) {
                            GotAssoc = 1;
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
                IwlStallMs(1);
            }
            if (!GotAssoc) {
                char Line[28];
                char Hex[12];
                int n = 0;
                const char *P = "assoc=to n=";
                while (*P) {
                    Line[n++] = *P++;
                }
                HalSerialFormatHex(Hex, RxN & 0xffu, 2);
                Line[n++] = Hex[2];
                Line[n++] = Hex[3];
                Line[n++] = ' ';
                Line[n++] = 'f';
                Line[n++] = '=';
                HalSerialFormatHex(Hex, FirstFc, 2);
                Line[n++] = Hex[2];
                Line[n++] = Hex[3];
                Line[n] = 0;
                IwlLogStage(Line);
                if (RxN == 0) {
                    (void)IwlPhyCtxtTune(gIwlTarget.Chan);
                }
                if (Attempt < 3) {
                    IwlLogStage("assoc=retry");
                }
            }
        }
    }
    if (!GotAssoc) {
        IwlLogStage("assoc=fail");
        return 0;
    }
    gIwlAssociated = 1;
    IwlLogStage("assoc=ok");
    /* assoc 后短抽，再捞一波抢跑 M1 */
    for (i = 0; i < 80; i++) {
        IwlRxPoll();
        while (IwlAssocTake(&Pkt, &Len)) {
            const UINT8 *Dot;
            UINTN PayLen;
            if (Pkt->Hdr.Code != IWL_RX_MPDU_CMD || Len < 12) {
                continue;
            }
            Dot = Pkt->Data + 4;
            PayLen = Len - sizeof(IWL_CMD_HDR) - 4;
            if (PayLen >= 24 && ((Dot[0] >> 2) & 0x3u) == 0x2u) {
                IwlRxHoldMpdu(Pkt, Len);
            }
        }
        IwlStallMs(1);
    }
    return 1;
}
