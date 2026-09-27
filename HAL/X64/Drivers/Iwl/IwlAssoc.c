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
                /* 刀 #144：#140 首发双发能 auth=ok。#143 窗内再补发后重试 n=00。
                 * 仅第 0 轮双发；重试只发一帧，不再窗内补发。 */
                if (Attempt == 0) {
                    (void)IwlSendFrameRaw(Frame, Flen);
                }

                {
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
                        HalSerialFormatHex(Hex, RxN & 0xffu, 2);
                        Line[n++] = Hex[2];
                        Line[n++] = Hex[3];
                        Line[n++] = ' ';
                        Line[n++] = 'f';
                        Line[n++] = '=';
                        HalSerialFormatHex(Hex, FirstFc, 2);
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
                        /* 有 beacon 说明信道还在；phytune 会把后面打成 n=00 */
                        if (RxN == 0) {
                            (void)IwlCmdqUnwedge();
                            (void)IwlPhyCtxtTune(gIwlTarget.Chan);
                        }
                        continue;
                    }
                    IwlLogStage("auth=ok");
                    EverAuth = 1;
                }
            } else {
                IwlLogVerb("auth=keep");
            }

            /*
             * 刀 #139：#132 在 assoc 前 Prep → 公司 AP 上 assoc=to，首帧尽是 f=48 Null，
             * 从未见 AssocResp(0x10)。#109 是 assoc 后再 macadd 才 assoc=ok。
             * 认证已通；关联请求仍无 MAC 上下文，Prep 挪到 assoc=ok 之后。
             */
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

                /* 刀 #140：beacon 里的 HT，再 RSN（优先拷贝 AP 原件） */
                if (gIwlTarget.HtLen > 0 && P + 2 + gIwlTarget.HtLen <= sizeof(Pay)) {
                    Pay[P] = 45;
                    Pay[P + 1] = gIwlTarget.HtLen;
                    IwlCopyN(Pay + P + 2, gIwlTarget.Ht, gIwlTarget.HtLen);
                    P += 2 + gIwlTarget.HtLen;
                }
                if (gIwlTarget.HasRsn && gIwlPsk[0]) {
                    if (gIwlTarget.RsnLen >= 4 &&
                        P + gIwlTarget.RsnLen <= sizeof(Pay)) {
                        IwlCopyN(Pay + P, gIwlTarget.Rsn, gIwlTarget.RsnLen);
                        P += gIwlTarget.RsnLen;
                    } else {
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
                }
                IwlBuildMgmt(Frame, &Flen, 0x0, Pay, P);
            }
            if (Attempt == 0) {
                char Ie[20];
                int en = 0;
                const char *Ep = "assoc=ie h=";
                while (*Ep) {
                    Ie[en++] = *Ep++;
                }
                Ie[en++] = gIwlTarget.HtLen ? '1' : '0';
                Ie[en++] = ' ';
                Ie[en++] = 'r';
                Ie[en++] = '=';
                Ie[en++] = (gIwlTarget.RsnLen >= 4) ? '1' : '0';
                Ie[en] = 0;
                IwlLogStage(Ie);
            }
            /* 刀 #137：te=ok 之后原先要等满本次超时才有下一行黄字 */
            if (IwlSendFrameRaw(Frame, Flen) != 0) {
                IwlLogStage("assoc=txfail");
                return 0;
            }
            IwlLogStage("assoc=tx");
            /* 刀 #150：仅首轮双发。重试再双发、窗内再补发，会把收包打成 n=00。 */
            if (Attempt == 0) {
                (void)IwlSendFrameRaw(Frame, Flen);
            }
            /* 刀 #138：第二发若卡住，不会有 wait；0.4s 报一次已收帧数 */
            IwlLogStage("assoc=wait");
            RxN = 0;
            FirstFc = 0;
            {
                UINT32 BeaconN = 0;
                UINT32 RxLate = 0;

                for (i = 0; i < 2500 && !GotAssoc; i++) {
                    int Took = 0;

                    if (i == 2000u) {
                        RxLate = RxN;
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
                        if (Sub == 8u || Sub == 5u) {
                            BeaconN++;
                        }
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
                    if (i == 399u) {
                        char Line[28];
                        char Hex[12];
                        int n = 0;
                        const char *Pr = "assoc=rn n=";

                        while (*Pr) {
                            Line[n++] = *Pr++;
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
                    }
                    IwlStallMs(1);
                }
                if (!GotAssoc) {
                    char Line[32];
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
                    Line[n++] = ' ';
                    Line[n++] = 'b';
                    Line[n++] = '=';
                    HalSerialFormatHex(Hex, BeaconN & 0xffu, 2);
                    Line[n++] = Hex[2];
                    Line[n++] = Hex[3];
                    Line[n] = 0;
                    IwlLogStage(Line);
                    /*
                     * 刀 #142：#140 曾首轮 assoc=ok。#141 仅加黄字却「退步」：
                     * 首轮 to 仍见 beacon 时 Unwedge+phytune → 其后全程 n=00。
                     * 还有 beacon 就只重发 Assoc；收包归零才拉 RX。
                     */
                    /*
                     * 末 0.5s 没有新帧：环已停（本轮 n 仍可能是开头的 beacon）。
                     * 还在收 beacon 则只重发，不 phytune。
                     */
                    if (RxN == 0 || RxN == RxLate) {
                        IwlLogStage("assoc=rxstall");
                        (void)IwlCmdqUnwedge();
                        (void)IwlPhyCtxtTune(gIwlTarget.Chan);
                    }
                    if (Attempt < 3) {
                        IwlLogStage("assoc=retry");
                    }
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
    /* 刀 #139：assoc 成功后再 macadd+bind+TE，盖住随后 EAPOL 窗 */
    if (!IwlMacCtxtPrep()) {
        IwlLogStage("prep=soft");
    }
    /*
     * 刀 #145：#144 te=ok 后十余秒无 post=sta。
     * 这段抽环不封顶，MAC 起来后 beacon 灌满就出不去。
     */
    IwlLogStage("drain=go");
    for (i = 0; i < 80; i++) {
        int Took = 0;

        IwlRxPoll();
        while (Took < 32 && IwlAssocTake(&Pkt, &Len)) {
            const UINT8 *Dot;
            UINTN PayLen;

            Took++;
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
