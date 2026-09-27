/*
 * IwlEapol.c — WPA2-PSK 四次握手（教学最小）（PR-N-wifi-2）
 *
 * 刀 #85：先收 msg1 再 PBKDF2（4096 次太慢，原先会堵死 RX 环丢 msg1 → wpa2=fail）。
 * GTK 解包略（单播 ping 用 PTK）。永不串口打印 PSK。
 */
#include "IwlPrivate.h"
#include "HalSerial.h"

static void IwlZero(void *P, UINTN N) {
    UINT8 *B = (UINT8 *)P;
    UINTN i;
    for (i = 0; i < N; i++) {
        B[i] = 0;
    }
}

static void IwlCopyN(UINT8 *D, const UINT8 *S, UINTN N) {
    UINTN i;
    for (i = 0; i < N; i++) {
        D[i] = S[i];
    }
}

static int IwlMemCmp(const UINT8 *A, const UINT8 *B, UINTN N) {
    UINTN i;
    for (i = 0; i < N; i++) {
        if (A[i] != B[i]) {
            return (int)A[i] - (int)B[i];
        }
    }
    return 0;
}

/* IEEE 802.11i PRF-384 → 48 bytes (KCK|KEK|TK) from PMK */
static void IwlPrf384(const UINT8 Pmk[32], const UINT8 *A, UINTN Alen,
                     const UINT8 *B, UINTN Blen, UINT8 Out[48]) {
    UINT8 Buf[128];
    UINT8 Tmp[20];
    UINTN Pos = 0;
    UINT8 Count = 0;
    UINTN OutPos = 0;
    UINTN i;

    for (i = 0; i < Alen && Pos < sizeof(Buf); i++) {
        Buf[Pos++] = A[i];
    }
    if (Pos < sizeof(Buf)) {
        Buf[Pos++] = 0;
    }
    for (i = 0; i < Blen && Pos < sizeof(Buf); i++) {
        Buf[Pos++] = B[i];
    }
    while (OutPos < 48) {
        UINT8 Input[160];
        UINTN InLen = 0;
        for (i = 0; i < Pos; i++) {
            Input[InLen++] = Buf[i];
        }
        Input[InLen++] = Count;
        IwlHmacSha1(Pmk, 32, Input, InLen, Tmp);
        for (i = 0; i < 20 && OutPos < 48; i++) {
            Out[OutPos++] = Tmp[i];
        }
        Count++;
    }
}

static void IwlBuildPtk(const UINT8 Pmk[32], const UINT8 *Anon, const UINT8 *Snon,
                        UINT8 Ptk[48]) {
    UINT8 A[22];
    UINT8 B[76];
    UINTN i;
    const char *Lab = "Pairwise key expansion";
    const UINT8 *MinMac;
    const UINT8 *MaxMac;
    const UINT8 *MinNon;
    const UINT8 *MaxNon;

    for (i = 0; i < 22; i++) {
        A[i] = (UINT8)Lab[i];
    }
    if (IwlMemCmp(gIwlMac, gIwlBssid, 6) < 0) {
        MinMac = gIwlMac;
        MaxMac = gIwlBssid;
    } else {
        MinMac = gIwlBssid;
        MaxMac = gIwlMac;
    }
    if (IwlMemCmp(Anon, Snon, 32) < 0) {
        MinNon = Anon;
        MaxNon = Snon;
    } else {
        MinNon = Snon;
        MaxNon = Anon;
    }
    IwlCopyN(B, MinMac, 6);
    IwlCopyN(B + 6, MaxMac, 6);
    IwlCopyN(B + 12, MinNon, 32);
    IwlCopyN(B + 44, MaxNon, 32);
    IwlPrf384(Pmk, A, 22, B, 76, Ptk);
}

/* EAPOL-Key MIC：HMAC-SHA1，取前 16 字节（Key Descriptor Ver 2） */
static void IwlEapolMic(const UINT8 Kck[16], UINT8 *Eapol, UINTN EapolLen,
                        UINT8 Mic[16]) {
    UINT8 Dig[20];
    UINTN i;
    for (i = 0; i < 16; i++) {
        Eapol[81 + i] = 0;
    }
    IwlHmacSha1(Kck, 16, Eapol, EapolLen, Dig);
    for (i = 0; i < 16; i++) {
        Mic[i] = Dig[i];
        Eapol[81 + i] = Dig[i];
    }
}

/* 802.11 data 头长（含 QoS / Addr4 / HT Ctrl / CCMP） */
static UINTN IwlDot11DataHdrLen(UINT16 Fc) {
    UINTN HdrLen = 24u;

    if ((Fc & 0x0300u) == 0x0300u) {
        HdrLen += 6u; /* Addr4 */
    }
    if ((Fc & 0x008Cu) == 0x0088u) {
        HdrLen += 2u; /* QoS */
        if (Fc & 0x8000u) {
            HdrLen += 4u; /* Order → HT Control */
        }
    }
    if (Fc & 0x4000u) {
        HdrLen += 8u; /* CCMP/TKIP IV（明文 EAPOL 无此位） */
    }
    return HdrLen;
}

/*
 * 在 data 帧里找 SNAP 88 8E → EAPOL。
 * 刀 #116：Order/HT、头后最多扫 32B（防对齐/垫片）；返回 1=找到。
 */
static int IwlFindEapol(UINT8 *Dot11, UINTN FLen, UINT8 **OutEap, UINTN *OutLen) {
    UINT16 Fc;
    UINTN HdrLen;
    UINTN Off;
    UINTN End;
    UINTN BodyLen;
    UINT16 Elen;

    if (!Dot11 || FLen < 32) {
        return 0;
    }
    Fc = (UINT16)Dot11[0] | ((UINT16)Dot11[1] << 8);
    if (((Fc >> 2) & 0x3u) != 0x2u) {
        return 0;
    }
    /* QoS Null / Null：无载荷 */
    if ((Fc & 0x00FCu) == 0x00C8u || (Fc & 0x00FCu) == 0x0048u) {
        return 0;
    }
    HdrLen = IwlDot11DataHdrLen(Fc);
    if (FLen < HdrLen + 8u + 4u) {
        return 0;
    }
    End = HdrLen + 32u;
    if (End + 8u > FLen) {
        End = FLen - 8u;
    }
    for (Off = HdrLen; Off <= End; Off++) {
        if (Dot11[Off] != 0xAA || Dot11[Off + 1] != 0xAA) {
            continue;
        }
        if (Dot11[Off + 6] != 0x88 || Dot11[Off + 7] != 0x8E) {
            continue;
        }
        Off += 8;
        BodyLen = FLen - Off;
        if (BodyLen < 4u || Dot11[Off + 1] != 0x03) {
            return 0;
        }
        Elen = (UINT16)(((UINT16)Dot11[Off + 2] << 8) | Dot11[Off + 3]);
        if ((UINTN)(4u + Elen) > BodyLen) {
            return 0;
        }
        if ((UINTN)(4u + Elen) < 99u) {
            return 0; /* Start/其它短帧；msg1 至少 99 */
        }
        *OutEap = Dot11 + Off;
        *OutLen = 4u + (UINTN)Elen;
        return 1;
    }
    return 0;
}

static int IwlAddr1IsUs(const UINT8 *Dot11) {
    UINTN i;

    for (i = 0; i < 6; i++) {
        if (Dot11[4 + i] != gIwlMac[i]) {
            return 0;
        }
    }
    return 1;
}

/* 抽干当前 RX 环；找到 EAPOL 返回 1，环空且未找到返回 0 */
static int IwlRxDrainForEapol(UINT8 *EapOut, UINTN *EapLenOut, UINTN Cap) {
    IWL_RX_PKT *Pkt;
    UINTN Len;

    while (IwlRxTake(&Pkt, &Len)) {
        UINT8 Mutable[512];
        UINTN FLen;
        UINT8 *Ep;
        UINTN EpLen;
        UINTN k;

        if (Pkt->Hdr.Code != IWL_RX_MPDU_CMD || Len < 8) {
            continue;
        }
        FLen = Len - sizeof(IWL_CMD_HDR) - 4;
        if (FLen > sizeof(Mutable)) {
            FLen = sizeof(Mutable);
        }
        for (k = 0; k < FLen; k++) {
            Mutable[k] = Pkt->Data[4 + k];
        }
        if (IwlFindEapol(Mutable, FLen, &Ep, &EpLen) && EpLen >= 99
            && EpLen <= Cap) {
            IwlCopyN(EapOut, Ep, EpLen);
            *EapLenOut = EpLen;
            return 1;
        }
    }
    return 0;
}

static int IwlSendEapol(const UINT8 *Eapol, UINTN EapolLen) {
    UINT8 Frame[320];
    UINTN i;
    UINTN Flen;

    if (EapolLen > 280) {
        return -1;
    }
    Frame[0] = 0x08;
    Frame[1] = 0x01;
    Frame[2] = 0;
    Frame[3] = 0;
    IwlCopyN(Frame + 4, gIwlBssid, 6);
    IwlCopyN(Frame + 10, gIwlMac, 6);
    IwlCopyN(Frame + 16, gIwlBssid, 6);
    Frame[22] = 0;
    Frame[23] = 0;
    Frame[24] = 0xAA;
    Frame[25] = 0xAA;
    Frame[26] = 0x03;
    Frame[27] = 0;
    Frame[28] = 0;
    Frame[29] = 0;
    Frame[30] = 0x88;
    Frame[31] = 0x8E;
    for (i = 0; i < EapolLen; i++) {
        Frame[32 + i] = Eapol[i];
    }
    Flen = 32 + EapolLen;
    return IwlSendFrameRaw(Frame, Flen);
}

/* ToDS Null（PM=0）：促 AP 清省电缓冲，再发 EAPOL-Start */
static void IwlSendNullPm0(void) {
    UINT8 Frame[24];

    IwlZero(Frame, sizeof(Frame));
    Frame[0] = 0x48; /* Data Null */
    Frame[1] = 0x01; /* ToDS */
    IwlCopyN(Frame + 4, gIwlBssid, 6);
    IwlCopyN(Frame + 10, gIwlMac, 6);
    IwlCopyN(Frame + 16, gIwlBssid, 6);
    (void)IwlSendFrameRaw(Frame, 24);
}

static UINT16 IwlBe16(const UINT8 *P) {
    return (UINT16)(((UINT16)P[0] << 8) | P[1]);
}

static void IwlPutBe16(UINT8 *P, UINT16 V) {
    P[0] = (UINT8)(V >> 8);
    P[1] = (UINT8)V;
}

int IwlEapolRun(void) {
    UINT8 Pmk[32];
    UINT8 Ptk[48];
    UINT8 Kck[16];
    UINT8 Anonce[32];
    UINT8 Snonce[32];
    UINT8 Replay[8];
    UINT8 Eapol[256];
    UINTN EapLen = 0;
    UINT32 i;
    int Got1 = 0;
    int Got3 = 0;
    UINTN SsidLen;
    UINT16 KeyInfo;

    gIwlWpa2Ok = 0;
    IwlZero(Pmk, 32);
    IwlZero(Ptk, 48);
    IwlZero(Kck, 16);
    IwlZero(Anonce, 32);
    IwlZero(Snonce, 32);
    IwlZero(Replay, 8);

    if (!gIwlAssociated || !gIwlPsk[0] || !gIwlSsid[0]) {
        IwlLogStage("wpa2=nocfg");
        return 0;
    }

    /*
     * 刀 #103：m1to d=00 f=8000=仅 beacon。macadd/bind 后 AP 已发过 msg1。
     * 发 EAPOL-Start 促重传；等待中每 500ms 再啄一次。
     * 刀 #116：#115 后 d=18 有 data；补 u=单播-to-us、eap=命中但非 M1、首单播 hex。
     */
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
            char Hline[16];
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
        }
        IwlSendNullPm0();
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

        for (i = 0; i < 4000 && !Got1; i++) {
            IWL_RX_PKT *Pkt;
            UINTN Len;

            if ((i % 500u) == 499u) {
                IwlSendNullPm0();
                (void)IwlSendEapol(Start, 4);
            }

            IwlRxPoll();
            for (;;) {
                UINT8 Mutable[512];
                UINTN FLen;
                UINT8 *Ep;
                UINTN EpLen;
                UINTN k;
                UINT16 Fc;
                int ToUs;

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
                    && EpLen <= sizeof(Eapol)) {
                    EapHit++;
                    IwlCopyN(Eapol, Ep, EpLen);
                    EapLen = EpLen;
                    if (Eapol[4] == 2 || Eapol[4] == 254) {
                        KeyInfo = IwlBe16(Eapol + 5);
                        LastKi = KeyInfo;
                        /* M1：Ack、无 MIC；Accept Install=0 或偶发脏位 */
                        if ((KeyInfo & 0x0080u) != 0 && (KeyInfo & 0x0100u) == 0) {
                            IwlCopyN(Anonce, Eapol + 17, 32);
                            IwlCopyN(Replay, Eapol + 9, 8);
                            Got1 = 1;
                            break;
                        }
                    } else {
                        LastKi = (UINT16)Eapol[4];
                    }
                }
            }
            if (!Got1) {
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
    IwlLogStage("wpa2=m1");

    if (gIwlPmkOk) {
        IwlCopyN(Pmk, gIwlPmk, 32);
    } else {
        SsidLen = 0;
        while (gIwlSsid[SsidLen]) {
            SsidLen++;
        }
        if (!IwlPbkdf2Sha1(gIwlPsk, (const UINT8 *)gIwlSsid, SsidLen, 4096, Pmk, 32)) {
            IwlLogStage("wpa2=pmk");
            return 0;
        }
    }

    for (i = 0; i < 32; i++) {
        Snonce[i] = (UINT8)(gIwlMac[i % 6] ^ (UINT8)(i * 17u + 3u));
    }
    IwlBuildPtk(Pmk, Anonce, Snonce, Ptk);
    IwlCopyN(Kck, Ptk, 16);
    IwlCopyN(gIwlPtk, Ptk + 32, 16);
    IwlCopyN(gIwlGtk, Ptk + 32, 16);

    /* msg2：尽快回，AP 等 MIC */
    IwlZero(Eapol, sizeof(Eapol));
    Eapol[0] = 1;
    Eapol[1] = 3;
    IwlPutBe16(Eapol + 2, 95);
    Eapol[4] = 2;
    IwlPutBe16(Eapol + 5, 0x010A);
    IwlPutBe16(Eapol + 7, 16);
    IwlCopyN(Eapol + 9, Replay, 8);
    IwlCopyN(Eapol + 17, Snonce, 32);
    IwlPutBe16(Eapol + 97, 0);
    {
        UINT8 Mic[16];
        IwlEapolMic(Kck, Eapol, 99, Mic);
        (void)Mic;
    }
    if (IwlSendEapol(Eapol, 99) != 0) {
        IwlLogStage("wpa2=m2tx");
        return 0;
    }
    IwlLogStage("wpa2=m2");

    for (i = 0; i < 4000 && !Got3; i++) {
        IwlRxPoll();
        while (!Got3 && IwlRxDrainForEapol(Eapol, &EapLen, sizeof(Eapol))) {
            UINT8 Calc[16];
            UINT8 Saved[16];
            UINTN k;
            int Diff;

            if (Eapol[4] != 2 && Eapol[4] != 254) {
                continue;
            }
            KeyInfo = IwlBe16(Eapol + 5);
            if ((KeyInfo & 0x0100u) == 0 || (KeyInfo & 0x0080u) == 0) {
                continue;
            }
            IwlCopyN(Saved, Eapol + 81, 16);
            IwlEapolMic(Kck, Eapol, EapLen, Calc);
            Diff = 0;
            for (k = 0; k < 16; k++) {
                Diff |= (int)(Calc[k] ^ Saved[k]);
            }
            if (Diff != 0) {
                IwlLogStage("wpa2=mic");
                continue;
            }
            IwlCopyN(Replay, Eapol + 9, 8);
            Got3 = 1;
        }
        if (!Got3) {
            IwlStallMs(1);
        }
    }
    if (!Got3) {
        IwlLogStage("wpa2=m3to");
        return 0;
    }
    IwlLogStage("wpa2=m3");

    IwlZero(Eapol, sizeof(Eapol));
    Eapol[0] = 1;
    Eapol[1] = 3;
    IwlPutBe16(Eapol + 2, 95);
    Eapol[4] = 2;
    IwlPutBe16(Eapol + 5, 0x030A);
    IwlPutBe16(Eapol + 7, 16);
    IwlCopyN(Eapol + 9, Replay, 8);
    IwlPutBe16(Eapol + 97, 0);
    {
        UINT8 Mic[16];
        IwlEapolMic(Kck, Eapol, 99, Mic);
        (void)Mic;
    }
    if (IwlSendEapol(Eapol, 99) != 0) {
        IwlLogStage("wpa2=m4tx");
        return 0;
    }

    gIwlWpa2Ok = 1;
    IwlLogStage("wpa2=ok");
    return 1;
}
