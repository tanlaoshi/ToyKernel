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
                        UINT8 Ptk[48], UINT8 Ver) {
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
    if (Ver == 3) {
        IwlKdfSha256(Pmk, 32, Lab, B, 76, Ptk, 48);
    } else {
        IwlPrf384(Pmk, A, 22, B, 76, Ptk);
    }
}

/* EAPOL-Key MIC：版本 2 = HMAC-SHA1-128；版本 3 = AES-128-CMAC */
static void IwlEapolMic(UINT8 Ver, const UINT8 Kck[16], UINT8 *Eapol,
                        UINTN EapolLen, UINT8 Mic[16]) {
    UINT8 Dig[32];
    UINTN i;
    for (i = 0; i < 16; i++) {
        Eapol[81 + i] = 0;
        Dig[i] = 0;
    }
    if (Ver == 3) {
        IwlAesCmac(Kck, Eapol, EapolLen, Dig);
    } else {
        IwlHmacSha1(Kck, 16, Eapol, EapolLen, Dig);
    }
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

/* 刀 #149：M2 发出后固件的 TX 回执。0=还没看到 */
static int gIwlTxRsp;

static int gIwlRxCode;

static void IwlLogTxRsp(const IWL_RX_PKT *Pkt, UINTN Len) {
    char Line[40];
    char Hex[12];
    int n = 0;
    const char *P = "txa=l=";
    UINT8 Count = 0;

    if (gIwlTxRsp || !Pkt || Pkt->Hdr.Code != IWL_CMD_TX) {
        return;
    }
    if (Len >= sizeof(IWL_CMD_HDR) + 1u) {
        Count = Pkt->Data[0];
    }
    gIwlTxRsp = 1;
    while (*P) {
        Line[n++] = *P++;
    }
    HalSerialFormatHex(Hex, (UINT32)Len & 0xffu, 2);
    Line[n++] = Hex[2];
    Line[n++] = Hex[3];
    Line[n++] = ' ';
    Line[n++] = 'c';
    Line[n++] = '=';
    HalSerialFormatHex(Hex, Count, 2);
    Line[n++] = Hex[2];
    Line[n++] = Hex[3];
    if (Len >= sizeof(IWL_CMD_HDR) + 4u) {
        Line[n++] = ' ';
        Line[n++] = 'f';
        Line[n++] = '=';
        HalSerialFormatHex(Hex, Pkt->Data[3], 2);
        Line[n++] = Hex[2];
        Line[n++] = Hex[3];
    }
    if (Len >= sizeof(IWL_CMD_HDR) + 38u) {
        UINT16 St = (UINT16)Pkt->Data[36] | ((UINT16)Pkt->Data[37] << 8);
        Line[n++] = ' ';
        Line[n++] = 's';
        Line[n++] = '=';
        HalSerialFormatHex(Hex, St, 4);
        Line[n++] = Hex[2];
        Line[n++] = Hex[3];
        Line[n++] = Hex[4];
        Line[n++] = Hex[5];
    }
    Line[n] = 0;
    IwlLogStage(Line);
}

static void IwlLogRxCode(const IWL_RX_PKT *Pkt, UINTN Len) {
    char Line[36];
    char Hex[12];
    int n = 0;
    const char *P = "rxc=";
    UINTN Pay;
    UINTN i;

    if (gIwlRxCode || !Pkt) {
        return;
    }
    gIwlRxCode = 1;
    while (*P) {
        Line[n++] = *P++;
    }
    HalSerialFormatHex(Hex, Pkt->Hdr.Code, 2);
    Line[n++] = Hex[2];
    Line[n++] = Hex[3];
    Line[n++] = ' ';
    Line[n++] = 'l';
    Line[n++] = '=';
    HalSerialFormatHex(Hex, (UINT32)Len & 0xffu, 2);
    Line[n++] = Hex[2];
    Line[n++] = Hex[3];
    if (Pkt->Hdr.Code == 0xf7u) {
        Pay = 0;
        if (Len > sizeof(IWL_CMD_HDR)) {
            Pay = Len - sizeof(IWL_CMD_HDR);
        }
        if (Pay > 8u) {
            Pay = 8u;
        }
        Line[n++] = ' ';
        Line[n++] = 'd';
        Line[n++] = '=';
        for (i = 0; i < Pay; i++) {
            HalSerialFormatHex(Hex, Pkt->Data[i], 2);
            Line[n++] = Hex[2];
            Line[n++] = Hex[3];
        }
    }
    Line[n] = 0;
    IwlLogStage(Line);
}

/* 抽干当前 RX 环；找到 EAPOL 返回 1，环空且未找到返回 0 */
static int IwlRxDrainForEapol(UINT8 *EapOut, UINTN *EapLenOut, UINTN Cap) {
    IWL_RX_PKT *Pkt;
    UINTN Len;
    int Took = 0;

    while (Took < 32 && IwlRxTake(&Pkt, &Len)) {
        UINT8 Mutable[512];
        UINTN FLen;
        UINT8 *Ep;
        UINTN EpLen;
        UINTN k;

        Took++;
        if (Pkt->Hdr.Code == IWL_CMD_TX) {
            IwlLogTxRsp(Pkt, Len);
            continue;
        }
        if (Pkt->Hdr.Code != IWL_RX_MPDU_CMD) {
            IwlLogRxCode(Pkt, Len);
            continue;
        }
        if (Len < 8) {
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
    /* 非 QoS 数据。关联后 SendFrameRaw 放到 q5+AP_STA，速率 6M。 */
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

/* 刀 #163：M3 密钥数据是 KEK 包起来的 GTK KDE。 */
static void IwlInstallGtk(const UINT8 *Eapol, UINTN EapLen, const UINT8 Kek[16]) {
    UINT16 Kd;
    UINT8 Plain[48];
    UINTN PlainLen = 0;
    UINTN i;

    if (EapLen < 101u) {
        IwlLogStage("gtk=no");
        return;
    }
    Kd = (UINT16)(((UINT16)Eapol[97] << 8) | Eapol[98]);
    if (Kd < 16u || 99u + (UINTN)Kd > EapLen) {
        IwlLogStage("gtk=no");
        return;
    }
    if (!IwlAesUnwrap(Kek, Eapol + 99, Kd, Plain, sizeof(Plain), &PlainLen)) {
        IwlLogStage("gtk=bad");
        return;
    }
    for (i = 0; i + 22u <= PlainLen; i++) {
        if (Plain[i] == 0x00u && Plain[i + 1] == 0x0fu
            && Plain[i + 2] == 0xacu && Plain[i + 3] == 0x01u) {
            IwlCopyN(gIwlGtk, Plain + i + 6, 16);
            IwlLogStage("gtk=ok");
            return;
        }
    }
    IwlLogStage("gtk=no");
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
    UINT8 KeyDesc = 2;
    UINT8 KeyVer = 2;
    UINT8 EapVer = 1;

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
                            KeyDesc = Eapol[4];
                            Got1 = 1;
                            break;
                        }
                    } else {
                        LastKi = (UINT16)Eapol[4];
                    }
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
    {
        char Line[20];
        char Hex[12];
        int n = 0;
        const char *P = "wpa2=m1 k=";
        while (*P) {
            Line[n++] = *P++;
        }
        HalSerialFormatHex(Hex, (KeyInfo >> 8) & 0xffu, 2);
        Line[n++] = Hex[2];
        Line[n++] = Hex[3];
        HalSerialFormatHex(Hex, KeyInfo & 0xffu, 2);
        Line[n++] = Hex[2];
        Line[n++] = Hex[3];
        Line[n] = 0;
        IwlLogStage(Line);
        KeyVer = (UINT8)(KeyInfo & 7u);
        if (KeyVer != 3) {
            KeyVer = 2;
        }
        EapVer = Eapol[0];
        if (EapVer < 1 || EapVer > 2) {
            EapVer = 1;
        }
    }

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
    IwlBuildPtk(Pmk, Anonce, Snonce, Ptk, KeyVer);
    IwlCopyN(Kck, Ptk, 16);
    IwlCopyN(gIwlPtk, Ptk + 32, 16);
    IwlCopyN(gIwlGtk, Ptk + 32, 16);

    /* 刀 #146：M2 带 beacon RSN。刀 #147：KeyInfo 版本跟 M1（2=HMAC-SHA1，3=AES-CMAC）。 */
    IwlZero(Eapol, sizeof(Eapol));
    Eapol[0] = EapVer;
    Eapol[1] = 3;
    Eapol[4] = KeyDesc;
    IwlPutBe16(Eapol + 5, (UINT16)(0x0108u | KeyVer));
    IwlPutBe16(Eapol + 7, 16);
    IwlCopyN(Eapol + 9, Replay, 8);
    IwlCopyN(Eapol + 17, Snonce, 32);
    {
        UINTN Total = 99;
        UINT8 M2[160];
        UINT8 Kd = gIwlTarget.RsnLen;
        UINT32 Eap3 = 0;
        int SawKi = 0;
        int MicLogged = 0;

        if (Kd >= 4 && Kd <= 48 && 99u + Kd <= sizeof(Eapol)) {
            IwlCopyN(Eapol + 99, gIwlTarget.Rsn, Kd);
            IwlPutBe16(Eapol + 97, Kd);
            IwlPutBe16(Eapol + 2, (UINT16)(95u + Kd));
            Total = 99u + Kd;
        } else {
            IwlPutBe16(Eapol + 97, 0);
            IwlPutBe16(Eapol + 2, 95);
            Kd = 0;
        }
        {
            UINT8 Mic[16];
            IwlEapolMic(KeyVer, Kck, Eapol, Total, Mic);
            (void)Mic;
        }
        if (IwlSendEapol(Eapol, Total) != 0) {
            IwlLogStage("wpa2=m2tx");
            return 0;
        }
        IwlCopyN(M2, Eapol, Total);
        if (KeyVer == 3 && Kd) {
            IwlLogStage("wpa2=m2c");
        } else {
            IwlLogStage(Kd ? "wpa2=m2r" : "wpa2=m2");
        }

        gIwlTxRsp = 0;
        gIwlRxCode = 0;
        for (i = 0; i < 4000 && !Got3; i++) {
            if (i == 1000u || i == 2000u) {
                (void)IwlSendEapol(M2, Total);
            }
            IwlRxPoll();
            while (!Got3 && IwlRxDrainForEapol(Eapol, &EapLen, sizeof(Eapol))) {
                UINT8 Calc[16];
                UINT8 Saved[16];
                UINTN k;
                int Diff;

                Eap3++;
                if (!SawKi) {
                    char Line[28];
                    char Hex[12];
                    int n = 0;
                    const char *P = "wpa2=ki d=";
                    UINT16 Ki = IwlBe16(Eapol + 5);
                    while (*P) {
                        Line[n++] = *P++;
                    }
                    HalSerialFormatHex(Hex, Eapol[4], 2);
                    Line[n++] = Hex[2];
                    Line[n++] = Hex[3];
                    Line[n++] = ' ';
                    Line[n++] = 'k';
                    Line[n++] = '=';
                    HalSerialFormatHex(Hex, (Ki >> 8) & 0xffu, 2);
                    Line[n++] = Hex[2];
                    Line[n++] = Hex[3];
                    HalSerialFormatHex(Hex, Ki & 0xffu, 2);
                    Line[n++] = Hex[2];
                    Line[n++] = Hex[3];
                    Line[n] = 0;
                    IwlLogStage(Line);
                    SawKi = 1;
                }
                if (Eapol[4] != 2 && Eapol[4] != 254) {
                    continue;
                }
                KeyInfo = IwlBe16(Eapol + 5);
                /* M1 重传只有 ACK、没有 MIC。MIC 位置位就验，不再要求 ACK。 */
                if ((KeyInfo & 0x0100u) == 0) {
                    continue;
                }
                IwlCopyN(Saved, Eapol + 81, 16);
                IwlEapolMic(KeyVer, Kck, Eapol, EapLen, Calc);
                Diff = 0;
                for (k = 0; k < 16; k++) {
                    Diff |= (int)(Calc[k] ^ Saved[k]);
                }
                if (Diff != 0) {
                    if (!MicLogged) {
                        char Line[20];
                        char Hex[12];
                        int n = 0;
                        const char *P = "wpa2=mic k=";
                        while (*P) {
                            Line[n++] = *P++;
                        }
                        HalSerialFormatHex(Hex, (KeyInfo >> 8) & 0xffu, 2);
                        Line[n++] = Hex[2];
                        Line[n++] = Hex[3];
                        HalSerialFormatHex(Hex, KeyInfo & 0xffu, 2);
                        Line[n++] = Hex[2];
                        Line[n++] = Hex[3];
                        Line[n] = 0;
                        IwlLogStage(Line);
                        MicLogged = 1;
                    }
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
            char Line[24];
            char Hex[12];
            int n = 0;
            const char *P = "wpa2=m3to e=";
            if (!gIwlTxRsp) {
                IwlLogApQ();
                IwlLogStage("txa=none");
            }
            while (*P) {
                Line[n++] = *P++;
            }
            HalSerialFormatHex(Hex, Eap3 & 0xffu, 2);
            Line[n++] = Hex[2];
            Line[n++] = Hex[3];
            Line[n] = 0;
            IwlLogStage(Line);
            return 0;
        }
    }
    IwlLogStage("wpa2=m3");
    IwlInstallGtk(Eapol, EapLen, Ptk + 16);

    IwlZero(Eapol, sizeof(Eapol));
    Eapol[0] = EapVer;
    Eapol[1] = 3;
    IwlPutBe16(Eapol + 2, 95);
    Eapol[4] = KeyDesc;
    IwlPutBe16(Eapol + 5, (UINT16)(0x0308u | KeyVer));
    IwlPutBe16(Eapol + 7, 16);
    IwlCopyN(Eapol + 9, Replay, 8);
    IwlPutBe16(Eapol + 97, 0);
    {
        UINT8 Mic[16];
        IwlEapolMic(KeyVer, Kck, Eapol, 99, Mic);
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
