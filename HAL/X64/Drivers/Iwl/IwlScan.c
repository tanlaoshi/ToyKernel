/*
 * IwlScan.c — UMAC 扫描并按 SSID 选 BSS（PR-N-wifi-2）
 *
 * V7 + ADAPTIVE_DWELL + 52 槽（TLV N_SCAN_CHANNELS）；被动扫。
 * 依赖 IwlMvmPostAlive（SCAN_CFG · aux）。
 */
#include "IwlPrivate.h"
#include "HalSerial.h"

IWL_BSS gIwlTarget;
int gIwlScanCount;
int gIwlSsidOk;

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

static int IwlStrEq(const char *A, const UINT8 *B, UINTN Bl) {
    UINTN i;
    for (i = 0; i < Bl; i++) {
        if (A[i] == 0 || (UINT8)A[i] != B[i]) {
            return 0;
        }
    }
    return A[Bl] == 0;
}

static void IwlPut16(UINT8 *P, UINT16 V) {
    P[0] = (UINT8)V;
    P[1] = (UINT8)(V >> 8);
}

static void IwlPut32(UINT8 *P, UINT32 V) {
    P[0] = (UINT8)V;
    P[1] = (UINT8)(V >> 8);
    P[2] = (UINT8)(V >> 16);
    P[3] = (UINT8)(V >> 24);
}

static UINT8 gLastPhyChan;

static int IwlParseBeacon(const UINT8 *Frame, UINTN Len) {
    UINTN Pos;
    UINT8 SsidLen = 0;
    UINT8 Ssid[IWL_SSID_MAX];
    UINT8 Bssid[6];
    UINT16 Caps = 0;
    int HasRsn = 0;
    UINT8 RatesLen = 0;
    UINT8 Rates[8];
    UINT8 ExtRatesLen = 0;
    UINT8 ExtRates[8];
    UINT8 HtLen = 0;
    UINT8 Ht[26];
    UINT8 RsnLen = 0;
    UINT8 Rsn[48];
    UINTN i;

    if (Len < 36) {
        return 0;
    }
    if ((Frame[0] & 0xFC) != 0x80 && (Frame[0] & 0xFC) != 0x50) {
        return 0;
    }
    for (i = 0; i < 6; i++) {
        Bssid[i] = Frame[16 + i];
    }
    Caps = (UINT16)Frame[34] | ((UINT16)Frame[35] << 8);
    Pos = 36;
    while (Pos + 2 <= Len) {
        UINT8 Id = Frame[Pos];
        UINT8 El = Frame[Pos + 1];
        if (Pos + 2 + El > Len) {
            break;
        }
        if (Id == 0 && El <= IWL_SSID_MAX) {
            SsidLen = El;
            IwlCopyN(Ssid, Frame + Pos + 2, El);
        } else if (Id == 1 && El >= 1) {
            /* Supported Rates：拷进 AssocReq，否则 AP 回 status=18 */
            RatesLen = El > 8 ? 8 : El;
            IwlCopyN(Rates, Frame + Pos + 2, RatesLen);
        } else if (Id == 3 && El >= 1) {
            gLastPhyChan = Frame[Pos + 2];
        } else if (Id == 50 && El >= 1) {
            ExtRatesLen = El > 8 ? 8 : El;
            IwlCopyN(ExtRates, Frame + Pos + 2, ExtRatesLen);
        } else if (Id == 45 && El >= 2) {
            /* 刀 #140：公司 AP 常要 HT Capabilities */
            HtLen = El > 26 ? 26 : El;
            IwlCopyN(Ht, Frame + Pos + 2, HtLen);
        } else if (Id == 48 && El >= 2 && (UINTN)El + 2u <= sizeof(Rsn)) {
            RsnLen = (UINT8)(El + 2);
            IwlCopyN(Rsn, Frame + Pos, RsnLen);
            HasRsn = 1;
        }
        Pos += 2 + El;
    }
    if (SsidLen == 0 || !IwlStrEq(gIwlSsid, Ssid, SsidLen)) {
        return 0;
    }
    IwlCopyN(gIwlTarget.Bssid, Bssid, 6);
    gIwlTarget.SsidLen = SsidLen;
    IwlCopyN(gIwlTarget.Ssid, Ssid, SsidLen);
    gIwlTarget.Caps = Caps;
    gIwlTarget.HasRsn = HasRsn;
    gIwlTarget.Chan = gLastPhyChan ? gLastPhyChan : 1;
    gIwlTarget.RatesLen = RatesLen;
    IwlCopyN(gIwlTarget.Rates, Rates, RatesLen);
    gIwlTarget.ExtRatesLen = ExtRatesLen;
    IwlCopyN(gIwlTarget.ExtRates, ExtRates, ExtRatesLen);
    gIwlTarget.HtLen = HtLen;
    IwlCopyN(gIwlTarget.Ht, Ht, HtLen);
    gIwlTarget.RsnLen = RsnLen;
    IwlCopyN(gIwlTarget.Rsn, Rsn, RsnLen);
    gIwlSsidOk = 1;
    return 1;
}

/* 失败黄字只用前 8 个可见字符，避免一行撑过串口宽度 */
static void IwlCopyVis(char *Dst, const UINT8 *Src, UINTN Len) {
    UINTN i;
    UINTN N = Len > 8u ? 8u : Len;

    Dst[0] = 0;
    for (i = 0; i < N; i++) {
        UINT8 Ch = Src[i];
        Dst[i] = (Ch >= 32u && Ch < 127u) ? (char)Ch : '.';
    }
    Dst[N] = 0;
}

static void IwlNoteBeacon(const UINT8 *Frame, UINTN Len, char *Heard) {
    UINTN Pos;

    if (Heard[0] != 0 || Len < 36u) {
        return;
    }
    Pos = 36;
    while (Pos + 2u <= Len) {
        UINT8 Id = Frame[Pos];
        UINT8 El = Frame[Pos + 1];
        if (Pos + 2u + El > Len) {
            break;
        }
        if (Id == 0) {
            IwlCopyVis(Heard, Frame + Pos + 2, El);
            return;
        }
        Pos += 2u + El;
    }
}

/*
 * 刀 #74：#73 满尾 V1 仍无 ACK/0f。UCODE core33/API36 → OpenBSD 走 V7+ADAPTIVE；
 * 刀 #80：#79 uid 仍 to。UCODE TLV 0x1f=N_SCAN_CHANNELS=52，旧硬编码 40
 * → schedule/preq 错位 96B，FW 静默不 ACK。改 52 + 抬 OutLen 上限。
 */
#define IWL_SCAN_REQ_MAX 2048
#define IWL_SCAN_REQ_UMAC_SIZE_V7 48
#define IWL_SCAN_ADWELL_BUDGET_FULL 300
#define IWL_SCAN_ADWELL_N_APS 2
#define IWL_SCAN_ADWELL_N_APS_SOCIAL 10

static int IwlBuildUmacScan(UINT8 *Req, UINT32 *OutLen) {
    UINT8 *ChanData;
    UINT8 *Tail;
    UINT8 *Preq;
    UINT8 *Direct;
    UINT8 *Frm;
    UINT32 Gen;
    UINT32 i;
    static const UINT8 Chans[] = {
        1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13
    };
    UINT32 Nact = 13;

    IwlZero(Req, IWL_SCAN_REQ_MAX);
    IwlPut32(Req + 0, 0);
    /*
     * 刀 #79：#78 BC 仍 cmdto。旧 uid=1 → Linux UID_TYPE=SCHED；
     * OpenBSD 置 0（REGULAR）。改回 0。
     */
    IwlPut32(Req + 4, 0);
    /* 刀 #77：FW 有 EXT_SCAN_PRIORITY；Linux 用 EXT_6，旧 HIGH=2 可能被忽略 */
    IwlPut32(Req + 8, IWL_SCAN_PRIORITY_EXT_6);
    Gen = IWL_UMAC_SCAN_GEN_PASS_ALL | IWL_UMAC_SCAN_GEN_ITER_COMPLETE
        | IWL_UMAC_SCAN_GEN_ADAPTIVE_DWELL | IWL_UMAC_SCAN_GEN_PASSIVE;
    IwlPut16(Req + 12, (UINT16)Gen);
    Req[14] = 0;
    Req[15] = 0;
    /* v7 dwell / adwell（OpenBSD iwm_umac_scan） */
    Req[16] = 10;
    Req[17] = 110;
    Req[18] = 44;
    Req[19] = (UINT8)IWL_SCAN_ADWELL_N_APS;
    Req[20] = (UINT8)IWL_SCAN_ADWELL_N_APS_SOCIAL;
    Req[21] = 0;
    IwlPut16(Req + 22, (UINT16)IWL_SCAN_ADWELL_BUDGET_FULL);
    IwlPut32(Req + 24, 0);
    IwlPut32(Req + 28, 0);
    IwlPut32(Req + 32, 0);
    IwlPut32(Req + 36, 0);
    IwlPut32(Req + 40, IWL_SCAN_PRIORITY_EXT_6);
    Req[44] = 0;
    Req[45] = (UINT8)Nact;
    Req[46] = 0;
    Req[47] = 0;

    ChanData = Req + IWL_SCAN_REQ_UMAC_SIZE_V7;
    for (i = 0; i < IWL_SCAN_NCHAN_CAPA; i++) {
        UINT8 *C = ChanData + i * 8;
        if (i < Nact) {
            IwlPut32(C, 0);
            C[4] = Chans[i];
            C[5] = 1;
            IwlPut16(C + 6, 0);
        }
    }

    Tail = ChanData + IWL_SCAN_NCHAN_CAPA * 8;
    IwlPut16(Tail + 0, 0);
    Tail[2] = 1;
    Tail[3] = 0;
    IwlPut16(Tail + 8, 0);
    IwlPut16(Tail + 10, 0);

    Preq = Tail + 12;
    Frm = Preq + 16;
    Frm[0] = 0x40;
    Frm[1] = 0x00;
    Frm[2] = 0;
    Frm[3] = 0;
    for (i = 0; i < 6; i++) {
        Frm[4 + i] = 0xff;
    }
    IwlCopyN(Frm + 10, gIwlMac, 6);
    for (i = 0; i < 6; i++) {
        Frm[16 + i] = 0xff;
    }
    Frm[22] = 0;
    Frm[23] = 0;
    Frm[24] = 0;
    Frm[25] = 0;
    Frm[26] = 1;
    Frm[27] = 8;
    Frm[28] = 0x82;
    Frm[29] = 0x84;
    Frm[30] = 0x8b;
    Frm[31] = 0x96;
    Frm[32] = 0x0c;
    Frm[33] = 0x12;
    Frm[34] = 0x18;
    Frm[35] = 0x24;
    Frm[36] = 50;
    Frm[37] = 4;
    Frm[38] = 0x30;
    Frm[39] = 0x48;
    Frm[40] = 0x60;
    Frm[41] = 0x6c;
    IwlPut16(Preq + 0, 0);
    IwlPut16(Preq + 2, 26);
    IwlPut16(Preq + 4, 26);
    IwlPut16(Preq + 6, 16);
    IwlPut16(Preq + 8, 0);
    IwlPut16(Preq + 10, 0);
    IwlPut16(Preq + 12, 0);
    IwlPut16(Preq + 14, 0);

    Direct = Preq + 16 + IWL_SCAN_PROBE_REQ_SIZE;
    *OutLen = (UINT32)(Direct + IWL_PROBE_OPTION_MAX * IWL_SSID_IE_SIZE - Req);
    return 1;
}

int IwlScanRun(void) {
    static UINT8 Req[IWL_SCAN_REQ_MAX];
    UINT32 ReqLen = 0;
    UINT32 i;
    IWL_RX_PKT *Pkt;
    UINTN Len;
    UINT32 Codes[8];
    UINT32 Ncode = 0;
    int GotAck = 0;
    int GotDone = 0;
    int StopAt = -1;
    UINT32 BeaconN = 0;
    UINT8 FirstFc = 0;
    char Heard[9];

    Heard[0] = 0;

    gIwlScanCount = 0;
    gIwlSsidOk = 0;
    gLastPhyChan = 0;
    IwlLogVerb("mvm=v83");

    if (!IwlBuildUmacScan(Req, &ReqLen) || ReqLen > IWL_CMD_PAYLOAD_MAX) {
        IwlLogStage("scan=build");
        return 0;
    }
    {
        char Line[24];
        char Hex[12];
        int n = 0;
        const char *P = "scan=L";
        while (*P) {
            Line[n++] = *P++;
        }
        HalSerialFormatHex(Hex, ReqLen, 4);
        Line[n++] = Hex[2];
        Line[n++] = Hex[3];
        Line[n++] = Hex[4];
        Line[n++] = Hex[5];
        Line[n] = 0;
        IwlLogVerb(Line);
    }
    /*
     * 刀 #77：#76 换序仍静默。改 sync（OpenBSD 前台扫）+ EXT_6；
     * 刀 #80：n_scan_channels=52（对齐 TLV）。
     */
    if (IwlSendCmd(IWL_CMD_ID(IWL_CMD_SCAN_REQ_UMAC, IWL_LONG_GROUP, 0),
                   Req, ReqLen, 1) != 0) {
        IwlLogStage("scan=to");
        IwlCmdqUnwedge();
        return 0;
    }
    GotAck = 1;
    IwlLogVerb("scan=ack");
    IwlCmdqSnap();

    for (i = 0; i < 8000; i++) {
        IwlRxPoll();
        while (IwlRxTake(&Pkt, &Len)) {
            const UINT8 *Raw = (const UINT8 *)&Pkt->Hdr;
            UINT8 Code = Pkt->Hdr.Code;
            UINT8 Flags = Pkt->Hdr.Flags;
            UINT8 Qid = Pkt->Hdr.Qid;
            int IsNotif = (Qid & 0x80u) != 0;
            UINTN HdrSz = (Len >= sizeof(IWL_CMD_HDR_WIDE) && Flags
                           && Len != sizeof(IWL_CMD_HDR))
                          ? sizeof(IWL_CMD_HDR_WIDE) : sizeof(IWL_CMD_HDR);
            const UINT8 *Payload = Raw + HdrSz;
            UINTN PayLen = Len > HdrSz ? Len - HdrSz : 0;
            gIwlScanCount++;
            if (Ncode < 8) {
                Codes[Ncode++] = Code | (IsNotif ? 0x100u : 0);
            }
            if (gIwlScanCount <= 8) {
                char Line[32];
                char Hex[12];
                int n = 0;
                const char *P = IsNotif ? "rxntf c=" : "rxack c=";
                while (*P) {
                    Line[n++] = *P++;
                }
                HalSerialFormatHex(Hex, Code, 2);
                Line[n++] = Hex[2];
                Line[n++] = Hex[3];
                Line[n++] = ' ';
                Line[n++] = 'i';
                Line[n++] = '=';
                HalSerialFormatHex(Hex, Pkt->Hdr.Idx, 2);
                Line[n++] = Hex[2];
                Line[n++] = Hex[3];
                Line[n] = 0;
                IwlLogVerb(Line);
            }
            if (gIwlScanCount <= 4) {
                char Line[56];
                char Hex[12];
                int n = 0;
                UINT32 k;
                const char *P = "rxraw L=";
                while (*P) {
                    Line[n++] = *P++;
                }
                HalSerialFormatHex(Hex, (UINT32)Len, 4);
                Line[n++] = Hex[2];
                Line[n++] = Hex[3];
                Line[n++] = Hex[4];
                Line[n++] = Hex[5];
                Line[n++] = ' ';
                Line[n++] = 'f';
                Line[n++] = '=';
                HalSerialFormatHex(Hex, Flags, 2);
                Line[n++] = Hex[2];
                Line[n++] = Hex[3];
                Line[n++] = ' ';
                for (k = 0; k < 16 && n < 52; k++) {
                    HalSerialFormatHex(Hex, Raw[k], 2);
                    Line[n++] = Hex[2];
                    Line[n++] = Hex[3];
                }
                Line[n] = 0;
                IwlLogVerb(Line);
            }
            if (!IsNotif && Code == IWL_CMD_SCAN_REQ_UMAC) {
                UINT32 St = 0;
                if (Len >= 4) {
                    St = (UINT32)Raw[Len - 4]
                       | ((UINT32)Raw[Len - 3] << 8)
                       | ((UINT32)Raw[Len - 2] << 16)
                       | ((UINT32)Raw[Len - 1] << 24);
                }
                GotAck = 1;
                if (St != 0) {
                    char Line[24];
                    char Hex[12];
                    int n = 0;
                    const char *P = "scan=st ";
                    while (*P) {
                        Line[n++] = *P++;
                    }
                    HalSerialFormatHex(Hex, St, 8);
                    Line[n++] = Hex[2];
                    Line[n++] = Hex[3];
                    Line[n++] = Hex[4];
                    Line[n++] = Hex[5];
                    Line[n++] = Hex[6];
                    Line[n++] = Hex[7];
                    Line[n++] = Hex[8];
                    Line[n++] = Hex[9];
                    Line[n] = 0;
                    IwlLogStage(Line);
                } else {
                    IwlLogStage("scan=ack");
                }
            }
            if (Code == 0x02u && PayLen >= 12) {
                char Line[48];
                char Hex[12];
                int n = 0;
                const char *P = "scerr e=";
                UINT32 Et = (UINT32)Payload[0] | ((UINT32)Payload[1] << 8)
                          | ((UINT32)Payload[2] << 16)
                          | ((UINT32)Payload[3] << 24);
                while (*P) {
                    Line[n++] = *P++;
                }
                HalSerialFormatHex(Hex, Et, 8);
                Line[n++] = Hex[2];
                Line[n++] = Hex[3];
                Line[n++] = Hex[4];
                Line[n++] = Hex[5];
                Line[n++] = Hex[6];
                Line[n++] = Hex[7];
                Line[n++] = Hex[8];
                Line[n++] = Hex[9];
                Line[n++] = ' ';
                Line[n++] = 'o';
                Line[n++] = '=';
                HalSerialFormatHex(Hex, Payload[4], 2);
                Line[n++] = Hex[2];
                Line[n++] = Hex[3];
                Line[n] = 0;
                IwlLogStage(Line);
                IwlCmdqSnap();
            }
            if (Code == 0xc0u && PayLen >= 24) {
                /* RX_PHY：channel 在 phy_info+22（le16） */
                gLastPhyChan = Payload[22];
            }
            /*
             * 刀 #81：#80 ack+C1+done 但 fail。8000 RX_MPDU =
             * iwl_rx_mpdu_res_start(4) + 802.11；旧试 +0/+16 漏 +4。
             * rxraw: …8101000C8000… → +4 起 FC=0x80 beacon。
             */
            if (Code == IWL_RX_MPDU_CMD || Code == 0xc3
                || Code == IWL_BEACON_NOTIFICATION) {
                const UINT8 *Frame = Payload;
                UINTN FLen = PayLen;
                if (Code == IWL_RX_MPDU_CMD && PayLen > 4) {
                    Frame = Payload + 4;
                    FLen = PayLen - 4;
                }
                if (Code == IWL_RX_MPDU_CMD && FLen > 0 && FirstFc == 0) {
                    FirstFc = Frame[0];
                }
                if (FLen > 32 &&
                    ((Frame[0] & 0xFCu) == 0x80u || (Frame[0] & 0xFCu) == 0x50u)) {
                    BeaconN++;
                    IwlNoteBeacon(Frame, FLen, Heard);
                }
                if (FLen > 32 && IwlParseBeacon(Frame, FLen)) {
                    IwlLogStage("scan=ok");
                    return 1;
                }
            }
            if (Code == IWL_SCAN_START_UMAC) {
                IwlLogVerb("scan=start");
            } else if (Code == IWL_SCAN_COMPLETE_UMAC
                       || Code == IWL_SCAN_COMPLETE_LMAC
                       || Code == IWL_SCAN_ITERATION_COMPLETE_UMAC) {
                GotDone = 1;
                if (gIwlSsidOk) {
                    IwlLogStage("scan=ok");
                    return 1;
                }
                /* 完成通知后面可能还有信标，先把本轮队列收完 */
                if (StopAt < 0) {
                    StopAt = (int)i + 400;
                }
            }
        }
        IwlStallMs(1);
        if (StopAt >= 0 && (int)i >= StopAt) {
            break;
        }
    }
    {
        char Line[56];
        char Hex[12];
        int n = 0;
        UINT32 k;
        const char *P = "scan=fail n=";
        while (*P) {
            Line[n++] = *P++;
        }
        HalSerialFormatHex(Hex, (UINT32)gIwlScanCount, 4);
        Line[n++] = Hex[2];
        Line[n++] = Hex[3];
        Line[n++] = Hex[4];
        Line[n++] = Hex[5];
        Line[n++] = ' ';
        Line[n++] = GotAck ? 'A' : 'a';
        Line[n++] = GotDone ? 'D' : 'd';
        for (k = 0; k < Ncode && n < 50; k++) {
            Line[n++] = ' ';
            HalSerialFormatHex(Hex, Codes[k] & 0xffu, 2);
            Line[n++] = Hex[2];
            Line[n++] = Hex[3];
            if (Codes[k] & 0x100u) {
                Line[n++] = 'n';
            }
        }
        Line[n] = 0;
        IwlLogStage(Line);
    }
    {
        char Line[64];
        char Hex[12];
        char Want[9];
        int n = 0;
        const char *P = "scan=miss b=";
        UINTN Wlen = 0;

        while (gIwlSsid[Wlen] && Wlen < 8u) {
            Wlen++;
        }
        IwlCopyVis(Want, (const UINT8 *)gIwlSsid, Wlen);
        while (*P && n < 48) {
            Line[n++] = *P++;
        }
        HalSerialFormatHex(Hex, BeaconN & 0xffu, 2);
        Line[n++] = Hex[2];
        Line[n++] = Hex[3];
        Line[n++] = ' ';
        Line[n++] = 'f';
        Line[n++] = 'c';
        Line[n++] = '=';
        HalSerialFormatHex(Hex, FirstFc, 2);
        Line[n++] = Hex[2];
        Line[n++] = Hex[3];
        Line[n++] = ' ';
        Line[n++] = 'h';
        Line[n++] = '=';
        P = Heard[0] ? Heard : "-";
        while (*P && n < 40) {
            Line[n++] = *P++;
        }
        Line[n++] = ' ';
        Line[n++] = 'w';
        Line[n++] = '=';
        P = Want[0] ? Want : "-";
        while (*P && n < 56) {
            Line[n++] = *P++;
        }
        Line[n] = 0;
        IwlLogStage(Line);
    }
    return 0;
}
