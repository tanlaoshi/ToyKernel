/*
 * IwlAssoc.c — open/WPA2 关联（auth → assoc）（PR-N-wifi-2）
 */
#include "IwlPrivate.h"
#include "HalSerial.h"

UINT8 gIwlStaRsn[32];
UINT8 gIwlStaRsnLen;

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

/*
 * 刀 #173：#140 把 AP beacon RSN 整段塞进 AssocReq。
 * 家里新路由常带 SAE/PMF；原样回显 → AP 回 status=12（0x000C）。
 * AssocReq 只声明 STA 真会的：CCMP + PSK；MFPC 跟 AP（不报 MFPR）。
 * 返回写入长度；0=无 PSK（AP 可能纯 WPA3）。
 */
static UINTN IwlBuildStaRsn(UINT8 *Out, UINTN Cap) {
    const UINT8 *R = gIwlTarget.Rsn;
    UINT8 Rl = gIwlTarget.RsnLen;
    UINTN Off;
    UINT16 PairCnt;
    UINT16 AkmCnt;
    UINT16 Caps = 0;
    int HasPsk = 0;
    int HasSae = 0;
    UINTN i;
    UINT8 Group[4];

    Group[0] = 0x00;
    Group[1] = 0x0f;
    Group[2] = 0xac;
    Group[3] = 0x04; /* CCMP 默认 */
    if (Rl >= 8 && R[0] == 48) {
        /*
         * 刀 #183 强制 CCMP 组播 → AP 不发 M1（m1to）。
         * 刀 #184：组播仍跟 beacon（常 TKIP=02）；DHCP 改单播走 PTK/CCMP。
         */
        {
            char Line[20];
            char Hex[12];
            int n = 0;
            const char *P = "assoc=gc=";
            while (*P) {
                Line[n++] = *P++;
            }
            HalSerialFormatHex(Hex, R[7], 2);
            Line[n++] = Hex[2];
            Line[n++] = Hex[3];
            Line[n] = 0;
            IwlLogStage(Line);
        }
        Group[0] = R[4];
        Group[1] = R[5];
        Group[2] = R[6];
        Group[3] = R[7];
        Off = 8;
        if (Off + 2 <= Rl) {
            PairCnt = (UINT16)R[Off] | ((UINT16)R[Off + 1] << 8);
            Off = Off + 2u + (UINTN)PairCnt * 4u;
        }
        if (Off + 2 <= Rl) {
            AkmCnt = (UINT16)R[Off] | ((UINT16)R[Off + 1] << 8);
            Off += 2;
            for (i = 0; i < AkmCnt && Off + 4u <= Rl; i++) {
                if (R[Off] == 0x00 && R[Off + 1] == 0x0f && R[Off + 2] == 0xac) {
                    if (R[Off + 3] == 0x02) {
                        HasPsk = 1;
                    }
                    if (R[Off + 3] == 0x08) {
                        HasSae = 1;
                    }
                }
                Off += 4;
            }
            if (Off + 2u <= Rl) {
                Caps = (UINT16)R[Off] | ((UINT16)R[Off + 1] << 8);
            }
        }
    } else {
        HasPsk = 1; /* 无可用 beacon RSN 时按旧静态 IE */
    }

    {
        char Line[28];
        char Hex[12];
        int n = 0;
        const char *P = "assoc=rsn p=";

        while (*P) {
            Line[n++] = *P++;
        }
        Line[n++] = HasPsk ? '1' : '0';
        Line[n++] = ' ';
        Line[n++] = 's';
        Line[n++] = '=';
        Line[n++] = HasSae ? '1' : '0';
        Line[n++] = ' ';
        Line[n++] = 'c';
        Line[n++] = '=';
        HalSerialFormatHex(Hex, (Caps >> 8) & 0xffu, 2);
        Line[n++] = Hex[2];
        Line[n++] = Hex[3];
        HalSerialFormatHex(Hex, Caps & 0xffu, 2);
        Line[n++] = Hex[2];
        Line[n++] = Hex[3];
        Line[n] = 0;
        IwlLogStage(Line);
    }

    if (!HasPsk) {
        IwlLogStage("assoc=wpa3");
        return 0;
    }

    /* STA RSN：只报 PSK；MFPC 跟随 AP，清除 MFPR（本驱动不做 802.11w） */
    if (Cap < 22u) {
        return 0;
    }
    Caps &= (UINT16)~(1u << 6); /* MFPR off */
    Out[0] = 48;
    Out[1] = 20;
    Out[2] = 0x01;
    Out[3] = 0x00;
    Out[4] = Group[0];
    Out[5] = Group[1];
    Out[6] = Group[2];
    Out[7] = Group[3];
    Out[8] = 0x01;
    Out[9] = 0x00;
    Out[10] = 0x00;
    Out[11] = 0x0f;
    Out[12] = 0xac;
    Out[13] = 0x04;
    Out[14] = 0x01;
    Out[15] = 0x00;
    Out[16] = 0x00;
    Out[17] = 0x0f;
    Out[18] = 0xac;
    Out[19] = 0x02;
    Out[20] = (UINT8)(Caps & 0xffu);
    Out[21] = (UINT8)((Caps >> 8) & 0xffu);
    /* 刀 #174：缓存给 M2，避免再塞 beacon 整段 */
    IwlCopyN(gIwlStaRsn, Out, 22);
    gIwlStaRsnLen = 22;
    return 22;
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
    gIwlStaRsnLen = 0;

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

                /* 刀 #140：beacon HT。刀 #173：RSN 改 STA 自建，勿回显 AP 整段 */
                if (gIwlTarget.HtLen > 0 && P + 2 + gIwlTarget.HtLen <= sizeof(Pay)) {
                    /* 家里 st000C 后重试：去掉 HT 再搏一次 */
                    if (Attempt == 0) {
                        Pay[P] = 45;
                        Pay[P + 1] = gIwlTarget.HtLen;
                        IwlCopyN(Pay + P + 2, gIwlTarget.Ht, gIwlTarget.HtLen);
                        P += 2 + gIwlTarget.HtLen;
                    }
                }
                if (gIwlTarget.HasRsn && gIwlPsk[0]) {
                    UINTN Rl = IwlBuildStaRsn(Pay + P, sizeof(Pay) - P);
                    if (Rl == 0) {
                        IwlLogStage("assoc=norsn");
                        return 0;
                    }
                    P += Rl;
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
