/*
 * Iwl.c — Setup / 状态 / 黄字（PR-N-wifi-2）
 */
#include "IwlPrivate.h"
#include "Hal.h"
#include "HalSerial.h"
#include "ToySerialLog.h"
#include "VirtualMemory.h"
#include "Net.h"

int gIwlReady;
int gIwlFwOk;
int gIwlBarOk;
int gIwlAlive;
int gIwlAssociated;
int gIwlWpa2Ok;
static int gDatTxLogged;
static int gDatRxLogged;
static int gDtxLogged;
static int gRxMicLogged;
static int gRxMicU;
static int gRxDiscLogged;
static int gRxOffLogged;
static int gRxLlcLogged;
static int gRxStLogged;
UINT16 gIwlDid;
UINT8 gIwlBus;
UINT8 gIwlDev;
UINT8 gIwlFn;
UINT8 gIwlMac[6];
UINT8 gIwlBssid[6];
UINT16 gIwlAid;
UINT8 gIwlPtk[16];
UINT8 gIwlGtk[16];
UINT8 gIwlGtkAlt[16];
UINT8 gIwlGtkId;
UINT8 gIwlGtkAltOk;
UINT8 gIwlTxStaId = IWL_AUX_STA_ID;

static void IwlAppend(char *Line, int *N, int Max, const char *S) {
    while (*S && *N < Max) {
        Line[(*N)++] = *S++;
    }
}

void IwlLogBound(void) {
    char Line[120];
    char Hex[12];
    int n = 0;

    IwlAppend(Line, &n, 100, "Boot: iwl8265 ");
    Line[n++] = 'b';
    Line[n++] = '=';
    HalSerialFormatHex(Hex, gIwlBus, 2);
    Line[n++] = Hex[2];
    Line[n++] = Hex[3];
    Line[n++] = ':';
    HalSerialFormatHex(Hex, gIwlDev, 2);
    Line[n++] = Hex[2];
    Line[n++] = Hex[3];
    Line[n++] = '.';
    HalSerialFormatHex(Hex, gIwlFn, 1);
    Line[n++] = Hex[2];
    Line[n++] = ' ';
    IwlAppend(Line, &n, 110, gIwlFwOk ? "fw=ok" : "fw=miss");
    Line[n++] = ' ';
    IwlAppend(Line, &n, 110, gIwlBarOk ? "bar=ok" : "bar=-");
    Line[n++] = ' ';
    IwlAppend(Line, &n, 110, gIwlAlive ? "alive=ok" : "alive=-");
    if (gIwlSsidOk) {
        Line[n++] = ' ';
        IwlAppend(Line, &n, 110, "ssid=ok");
    }
    if (gIwlAssociated) {
        Line[n++] = ' ';
        IwlAppend(Line, &n, 110, "assoc=ok");
    }
    if (gIwlWpa2Ok) {
        Line[n++] = ' ';
        IwlAppend(Line, &n, 110, "wpa2=ok");
    }
    Line[n++] = '\n';
    Line[n] = 0;
    ToyLogBoot(Line);
}

void IwlLogStage(const char *Tag) {
    char Line[80];
    int n = 0;
    IwlAppend(Line, &n, 70, "Boot: iwl8265 ");
    IwlAppend(Line, &n, 70, Tag);
    Line[n++] = '\n';
    Line[n] = 0;
    ToyLogBoot(Line);
}

void IwlLogVerb(const char *Tag) {
#if IWL_LOG_VERBOSE
    IwlLogStage(Tag);
#else
    (void)Tag;
#endif
}

static void IwlMakeLocalMac(void) {
    /* NVM 读后续补；先用本地管理地址避免全 0 */
    gIwlMac[0] = 0x02;
    gIwlMac[1] = 0x54;
    gIwlMac[2] = 0x4F;
    gIwlMac[3] = 0x59;
    gIwlMac[4] = (UINT8)gIwlBus;
    gIwlMac[5] = (UINT8)gIwlDev;
}

static int IwlBringUpSta(void) {
    int Hw;

    if (!VirtualMemoryEnabled()) {
        IwlLogStage("bar=novm");
        return 0;
    }
    Hw = IwlHwStart();
    if (Hw == -1) {
        IwlLogStage("prep=fail");
        return 0;
    }
    if (Hw != 1) {
        IwlLogStage("apm=fail");
        return 0;
    }
    /* Linux：nic_init（RX）在灌固件前，ALIVE 走 RX 通知 */
    if (!IwlNicInit()) {
        IwlLogStage("nic=fail");
        return 0;
    }
    {
        int Fw = IwlFwParseAndLoad();
        if (Fw != 1) {
            if (Fw == -2) {
                IwlLogStage("init_alive=fail");
            } else if (Fw == -1) {
                IwlLogStage("fwload=fail");
            } else {
                IwlLogStage("alive=fail");
            }
            return 0;
        }
    }
    if (!IwlCfgLoad()) {
        IwlLogStage("cfg=miss");
        return 0;
    }
    (void)IwlPmkPrepare(); /* 扫描前算 PMK，握手窗口只做 PTK */
    /*
     * 刀 #136：#135 在 scan=ok 之后才 mac=use。
     * 认证帧地址已是 94:B8:6D，SCAN_CFG 仍是本地地址 → auth=to n=18 f=80（只有 beacon）。
     * 扫描配置和认证帧用同一块芯片地址。
     */
    IwlReadHwMac();
    IwlApplyHwMac();
    if (!IwlMvmPostAlive()) {
        /* mvm=* 已打；仍尝试 scan 以观察 RX */
    }
    if (!IwlScanRun()) {
        return 0;
    }
    /* 刀 #132：Auth 无 MAC；#139/#140：assoc=ok 后再 Prep。接着 AP STA + EAPOL */
    if (!IwlAssocRun()) {
        IwlLogStage("assoc=fail");
        return 0;
    }
    IwlLogStage("post=sta");
    (void)IwlMacCtxtAssoc();
    IwlLogStage("post=eap");
    if (!IwlEapolRun()) {
        IwlLogStage("wpa2=fail");
        return 0;
    }
    return 1;
}

static int gIwlBgPending;
static int gIwlBgDone;
static int gIwlBgPhase; /* 0=待起/读 FW；1=待 BringUpSta */

int IwlStartSta(void) {
    return IwlBringUpSta();
}

int IwlBgBusy(void) {
    return !gIwlBgDone && (gIwlBgPending || gIwlBgPhase != 0);
}

int IwlBgStep(void) {
    if (gIwlBgDone) {
        return 1;
    }
    if (!gIwlBgPending && gIwlBgPhase == 0) {
        return 1;
    }

    /* 相 0：读 FW 后立刻还给 Worker，避免与上片粘成一次超长阻塞 */
    if (gIwlBgPhase == 0) {
        gIwlBgPending = 0;
        IwlLogStage("bg=start");
        if (!gIwlFwOk) {
            (void)IwlFwTryLoad();
        }
        if (!gIwlFwOk || !gIwlBarOk) {
            IwlLogStage(gIwlFwOk ? "bar=miss" : "fw=miss");
            gIwlBgDone = 1;
            IwlLogBound();
            return 1;
        }
        gIwlBgPhase = 1;
        IwlLogStage("bg=fw");
        return 0;
    }

    IwlLogStage("bg=up");
    (void)IwlBringUpSta();
    gIwlBgPhase = 0;
    gIwlBgDone = 1;
    IwlLogStage("bg=done");
    IwlLogBound();
    return 1;
}

int IwlSetup(void) {
    UINT8 Bus;
    UINT8 Dev;
    UINT8 Fn;
    UINT16 Did = 0;
    UINT64 Bar = 0;

    if (gIwlReady) {
        return 1;
    }
    if (!IwlPciFind(&Bus, &Dev, &Fn, &Bar, &Did)) {
        return 0;
    }
    gIwlBus = Bus;
    gIwlDev = Dev;
    gIwlFn = Fn;
    gIwlDid = Did;
    /* FW 大文件延到 IwlBgStep，Claim 只认 PCI+映 BAR */
    if (Bar && IwlMapBar(Bar)) {
        IwlMakeLocalMac();
        gIwlBarOk = 1;
    }
    gIwlReady = 1;
    /*
     * 刀 #114/#115：不在 Probe/开机路径上片关联（可卡数十秒）；
     * 亦不在此读 2.4MB UCODE（Live USB 上可卡数秒）。
     * Worker 后台 IwlBgStep；失败也不挡桌面。
     */
    if (gIwlBarOk) {
        gIwlBgPending = 1;
        /* #129：摘要留 bg=done，避免开机打两次 Bound */
    } else {
        IwlLogStage("defer=skip");
        IwlLogBound();
    }
    return 1;
}

int IwlReady(void) {
    return gIwlReady;
}

int IwlAssociated(void) {
    return gIwlAssociated && gIwlWpa2Ok;
}

void IwlGetMac(UINT8 Mac[6]) {
    int i;
    if (!Mac) {
        return;
    }
    for (i = 0; i < 6; i++) {
        Mac[i] = gIwlReady ? gIwlMac[i] : 0;
    }
}

UINT16 IwlPciDid(void) {
    return gIwlDid;
}

int IwlFwLoaded(void) {
    return gIwlFwOk;
}

int IwlSendFrame(const UINT8 *Frame, UINTN FrameLen) {
    /* 以太网 → 802.11 ToDS data + LLC；WPA2 时插 CCMP 头并加密 */
    UINT8 Wlan[420];
    UINTN i;
    UINTN BodyLen;
    UINTN WireLen;
    UINT64 Pn;
    static UINT64 gTxPn = 1;

    if (!IwlAssociated() || !Frame || FrameLen < 14 || FrameLen > 360) {
        return -1;
    }
    BodyLen = 8u + (FrameLen - 14u); /* LLC/SNAP + payload */
    /* FC data ToDS；WPA2 置 Protected */
    Wlan[0] = 0x08;
    Wlan[1] = (UINT8)(gIwlWpa2Ok ? 0x41u : 0x01u); /* ToDS | Protected? */
    Wlan[2] = 0;
    Wlan[3] = 0;
    for (i = 0; i < 6; i++) {
        Wlan[4 + i] = gIwlBssid[i];
    }
    for (i = 0; i < 6; i++) {
        Wlan[10 + i] = Frame[6 + i]; /* SA */
    }
    for (i = 0; i < 6; i++) {
        Wlan[16 + i] = Frame[i]; /* DA */
    }
    Wlan[22] = 0;
    Wlan[23] = 0;

    if (gIwlWpa2Ok) {
        /*
         * 队列 4 是用 tid 8 打开的，EAPOL 因此能被取走。
         * tid 6 的 QoS 帧停在环上。数据改回非 QoS，tid 仍由发送函数写成 8。
         */
        Pn = gTxPn++;
        Wlan[24] = (UINT8)Pn;
        Wlan[25] = (UINT8)(Pn >> 8);
        Wlan[26] = 0;
        Wlan[27] = 0x20; /* ExtIV，KeyID=0 */
        Wlan[28] = (UINT8)(Pn >> 16);
        Wlan[29] = (UINT8)(Pn >> 24);
        Wlan[30] = (UINT8)(Pn >> 32);
        Wlan[31] = (UINT8)(Pn >> 40);
        Wlan[32] = 0xAA;
        Wlan[33] = 0xAA;
        Wlan[34] = 0x03;
        Wlan[35] = 0;
        Wlan[36] = 0;
        Wlan[37] = 0;
        Wlan[38] = Frame[12];
        Wlan[39] = Frame[13];
        for (i = 0; i < FrameLen - 14; i++) {
            Wlan[40 + i] = Frame[14 + i];
        }
        /*
         * 明文交给固件加密。主机先算的 MIC 能通过自检，
         * 接入点只确认了 FCS，没有发回 Offer。
         */
        WireLen = 32u + BodyLen;
    } else {
        Wlan[24] = 0xAA;
        Wlan[25] = 0xAA;
        Wlan[26] = 0x03;
        Wlan[27] = 0;
        Wlan[28] = 0;
        Wlan[29] = 0;
        Wlan[30] = Frame[12];
        Wlan[31] = Frame[13];
        for (i = 0; i < FrameLen - 14; i++) {
            Wlan[32 + i] = Frame[14 + i];
        }
        WireLen = 24u + BodyLen;
    }
    if (IwlSendFrameRaw(Wlan, WireLen) != 0) {
        return -1;
    }
    if (!gDatTxLogged) {
        gDatTxLogged = 1;
        IwlLogStage("dat=fw");
    }
    return 0;
}

/* KeyID≠0 优先 GTK（组播 Offer）；KeyID=0 优先 PTK；刀 #182 再试 GtkAlt */
static int IwlDecryptEither(UINT8 *Frame, UINT64 Pn, UINTN MacHdr,
                            UINTN CryptBody, UINT8 KeyId) {
    if (KeyId != 0) {
        if (IwlCcmpDecrypt(gIwlGtk, Pn, Frame, MacHdr, CryptBody)) {
            return 1;
        }
        if (gIwlGtkAltOk
            && IwlCcmpDecrypt(gIwlGtkAlt, Pn, Frame, MacHdr, CryptBody)) {
            if (!gRxMicLogged) {
                IwlLogStage("gtk=o16");
            }
            return 1;
        }
        return IwlCcmpDecrypt(gIwlPtk, Pn, Frame, MacHdr, CryptBody);
    }
    if (IwlCcmpDecrypt(gIwlPtk, Pn, Frame, MacHdr, CryptBody)) {
        return 1;
    }
    if (IwlCcmpDecrypt(gIwlGtk, Pn, Frame, MacHdr, CryptBody)) {
        return 1;
    }
    if (gIwlGtkAltOk
        && IwlCcmpDecrypt(gIwlGtkAlt, Pn, Frame, MacHdr, CryptBody)) {
        return 1;
    }
    return 0;
}

static void IwlRxDataToNet(UINT8 *Frame, UINTN FLen, UINT32 St) {
    UINT16 Fc;
    UINTN HdrLen;
    UINTN BodyOff;
    UINTN BodyLen;
    UINT8 Eth[640];
    UINTN EthLen;
    UINTN i;
    UINT64 Pn;
    int Prot;
    int Qos;
    int FromDs;
    int ToDs;
    int FwDec;

    if (!Frame || FLen < 24) {
        return;
    }
    Fc = (UINT16)Frame[0] | ((UINT16)Frame[1] << 8);
    if (((Fc >> 2) & 0x3u) != 0x2u) {
        return; /* 非 data */
    }
    Prot = (Fc & 0x4000u) != 0;
    Qos = ((Fc & 0x008Cu) == 0x0088u);
    ToDs = (Fc & 0x0100u) != 0;
    FromDs = (Fc & 0x0200u) != 0;
    FwDec = (St & IWL_RX_MPDU_MIC_OK) != 0 && (St & IWL_RX_MPDU_DEC_DONE) != 0;
    HdrLen = 24u;
    if ((Fc & 0x0300u) == 0x0300u) {
        HdrLen += 6u;
    }
    if (Qos) {
        HdrLen += 2u;
        if ((Fc & 0x8000u) != 0) {
            HdrLen += 4u; /* HT Control */
        }
    }
    if (FLen < HdrLen + 8u) {
        return;
    }
    BodyOff = HdrLen;
    BodyLen = FLen - HdrLen;
    if (Prot) {
        /*
         * 刀 #180：固件已解（MIC_OK|DEC_DONE）→ 跳过主机 CCMP。
         * 布局仍 [mac][ccmp8][明文]；MIC 多半被 RADA 剥掉。
         */
        if (FwDec) {
            if (BodyLen < 8u) {
                return;
            }
            BodyOff = HdrLen + 8u;
            BodyLen = FLen - HdrLen - 8u;
            if (BodyLen >= 8u
                && !(Frame[BodyOff] == 0xAA && Frame[BodyOff + 1] == 0xAA)
                && BodyLen >= 16u) {
                BodyLen -= 8u; /* MIC 仍在 */
            }
        } else {
        if (BodyLen < 8u + 8u) {
            return; /* CCMP + MIC */
        }
        /* PN + KeyID from CCMP hdr */
        Pn = (UINT64)Frame[BodyOff]
           | ((UINT64)Frame[BodyOff + 1] << 8)
           | ((UINT64)Frame[BodyOff + 4] << 16)
           | ((UINT64)Frame[BodyOff + 5] << 24)
           | ((UINT64)Frame[BodyOff + 6] << 32)
           | ((UINT64)Frame[BodyOff + 7] << 40);
        /*
         * 刀 #178：MAC 头长单独传给 CCMP；勿把 CCMP 算进 HdrLen（QoS AAD）。
         * Layout [mac][ccmp8][body][mic8]。
         */
        {
            UINT8 KeyId = (UINT8)((Frame[BodyOff + 3] >> 6) & 3u);
            UINTN CryptBody = FLen - HdrLen - 8u - 8u;
            int Ok = IwlDecryptEither(Frame, Pn, HdrLen, CryptBody, KeyId);

            /* 刀 #179：byte_count 可能含 FCS(4)；再试 -4/-8 */
            if (!Ok && CryptBody > 4u) {
                Ok = IwlDecryptEither(Frame, Pn, HdrLen, CryptBody - 4u, KeyId);
                if (Ok) {
                    CryptBody -= 4u;
                }
            }
            if (!Ok && CryptBody > 8u) {
                Ok = IwlDecryptEither(Frame, Pn, HdrLen, CryptBody - 8u, KeyId);
                if (Ok) {
                    CryptBody -= 8u;
                }
            }
            if (!Ok) {
                {
                    int Us = 1;
                    int k;

                    for (k = 0; k < 6; k++) {
                        if (Frame[4 + k] != gIwlMac[k]) {
                            Us = 0;
                        }
                    }
                    if (Us) {
                        if (!gRxMicU) {
                            char Line[16];
                            char Hex[12];
                            UINT16 FcLog;
                            int n = 0;
                            const char *P = "rx=u";

                            gRxMicU = 1;
                            FcLog = (UINT16)Frame[0] | ((UINT16)Frame[1] << 8);
                            while (*P) {
                                Line[n++] = *P++;
                            }
                            HalSerialFormatHex(Hex, FcLog, 4);
                            Line[n++] = Hex[2];
                            Line[n++] = Hex[3];
                            Line[n++] = Hex[4];
                            Line[n++] = Hex[5];
                            Line[n] = 0;
                            IwlLogStage(Line);
                        }
                    } else if (!gRxMicLogged) {
                        char Line[28];
                        char Hex[12];
                        UINT16 FcLog;
                        int n = 0;
                        const char *P = "rx=mic o f=";

                        gRxMicLogged = 1;
                        FcLog = (UINT16)Frame[0] | ((UINT16)Frame[1] << 8);
                        while (*P) {
                            Line[n++] = *P++;
                        }
                        HalSerialFormatHex(Hex, FcLog, 4);
                        Line[n++] = Hex[2];
                        Line[n++] = Hex[3];
                        Line[n++] = Hex[4];
                        Line[n++] = Hex[5];
                        Line[n++] = ' ';
                        Line[n++] = 'k';
                        Line[n++] = '=';
                        HalSerialFormatHex(Hex, KeyId, 2);
                        Line[n++] = Hex[2];
                        Line[n++] = Hex[3];
                        Line[n++] = ' ';
                        Line[n++] = 'g';
                        Line[n++] = '=';
                        HalSerialFormatHex(Hex, gIwlGtkId, 2);
                        Line[n++] = Hex[2];
                        Line[n++] = Hex[3];
                        Line[n] = 0;
                        IwlLogStage(Line);
                    }
                }
                return;
            }
            BodyOff = HdrLen + 8u;
            BodyLen = CryptBody;
        }
        }
    }
    if (BodyLen < 8u) {
        return;
    }
    /* LLC/SNAP */
    if (Frame[BodyOff] != 0xAA || Frame[BodyOff + 1] != 0xAA) {
        if (!gRxLlcLogged) {
            gRxLlcLogged = 1;
            IwlLogStage("rx=llc");
        }
        return;
    }
    /* DA / SA */
    if (FromDs && !ToDs) {
        for (i = 0; i < 6; i++) {
            Eth[i] = Frame[4 + i];      /* Addr1 DA */
            Eth[6 + i] = Frame[16 + i]; /* Addr3 SA */
        }
    } else if (ToDs && !FromDs) {
        for (i = 0; i < 6; i++) {
            Eth[i] = Frame[16 + i];
            Eth[6 + i] = Frame[10 + i];
        }
    } else {
        return;
    }
    Eth[12] = Frame[BodyOff + 6];
    Eth[13] = Frame[BodyOff + 7];
    /* 0x888E 是 EAPOL，握手已经结束，不要送进 lwIP，也不要占掉第一帧记录 */
    if (Eth[12] == 0x88 && Eth[13] == 0x8E) {
        return;
    }
    EthLen = 14u + (BodyLen - 8u);
    if (EthLen > sizeof(Eth)) {
        return;
    }
    for (i = 0; i < BodyLen - 8u; i++) {
        Eth[14 + i] = Frame[BodyOff + 8 + i];
    }
    if (Eth[12] == 0x08 && Eth[13] == 0x00 && EthLen >= 38u && Eth[23] == 17u) {
        UINT16 Sp = (UINT16)(((UINT16)Eth[34] << 8) | Eth[35]);
        UINT16 Dp = (UINT16)(((UINT16)Eth[36] << 8) | Eth[37]);

        if (Sp == 0x0044u && Dp == 0x0043u) {
            if (!gRxDiscLogged) {
                gRxDiscLogged = 1;
                IwlLogStage("rx=disc");
            }
        } else if (Sp == 0x0043u && Dp == 0x0044u) {
            if (!gRxOffLogged) {
                gRxOffLogged = 1;
                IwlLogStage("rx=off");
            }
        } else if (!gDatRxLogged) {
            gDatRxLogged = 1;
            IwlLogStage("rx=0800");
        }
    } else if (!gDatRxLogged) {
        gDatRxLogged = 1;
        IwlLogStage("rx=oth");
    }
    NetInputFrame(Eth, EthLen);
}

static void IwlLogDataTx(const IWL_RX_PKT *Pkt, UINTN Len) {
    char Line[16];
    char Hex[12];
    UINT16 St;
    int n = 0;
    const char *P = "dtx=";

    /* 握手的 M4 回执会先到。发现包发出之后的第一帧才算数。 */
    if (!gDatTxLogged || gDtxLogged || !Pkt || Len < sizeof(IWL_CMD_HDR) + 38u) {
        return;
    }
    gDtxLogged = 1;
    St = (UINT16)Pkt->Data[36] | ((UINT16)Pkt->Data[37] << 8);
    while (*P) {
        Line[n++] = *P++;
    }
    HalSerialFormatHex(Hex, St, 4);
    Line[n++] = Hex[2];
    Line[n++] = Hex[3];
    Line[n++] = Hex[4];
    Line[n++] = Hex[5];
    Line[n] = 0;
    IwlLogStage(Line);
}

void IwlPoll(void) {
    IWL_RX_PKT *Pkt;
    UINTN Len;

    if (!gIwlAlive) {
        return;
    }
    IwlRxPoll();
    {
    int Took = 0;
    while (Took < 24 && IwlRxTake(&Pkt, &Len)) {
        UINT8 Code;
        const UINT8 *Payload;
        UINTN PayLen;
        UINT8 Mutable[512];
        UINTN i;

        Took++;
        if (!Pkt || Len < sizeof(IWL_CMD_HDR)) {
            continue;
        }
        Code = Pkt->Hdr.Code;
        if (Code == IWL_CMD_TX) {
            IwlLogDataTx(Pkt, Len);
        }
        Payload = Pkt->Data;
        PayLen = Len - sizeof(IWL_CMD_HDR);
        if (Code == IWL_RX_MPDU_CMD && PayLen > 4) {
            UINT16 Bc;
            UINTN FLen = PayLen - 4;
            UINT32 St = 0;

            /*
             * 刀 #170：byte_count 是 802.11 帧长。DMA 长度还含帧后的状态字
             * 和对齐填充，CCMP 会把 MIC 对到填充上。
             */
            Bc = (UINT16)Payload[0] | ((UINT16)Payload[1] << 8);
            if (Bc >= 24u && (UINTN)Bc <= FLen) {
                FLen = Bc;
            }
            /* 刀 #179：帧后 4B = RX_MPDU_RES_STATUS（MIC_OK bit6 / DEC_DONE bit11） */
            if (4u + FLen + 4u <= PayLen) {
                St = (UINT32)Payload[4 + FLen]
                   | ((UINT32)Payload[4 + FLen + 1] << 8)
                   | ((UINT32)Payload[4 + FLen + 2] << 16)
                   | ((UINT32)Payload[4 + FLen + 3] << 24);
            }
            if (FLen > sizeof(Mutable)) {
                FLen = sizeof(Mutable);
            }
            for (i = 0; i < FLen; i++) {
                Mutable[i] = Payload[4 + i];
            }
            if (gIwlWpa2Ok && !gRxStLogged && FLen >= 24u
                && (Mutable[1] & 0x40u) != 0) {
                char Line[20];
                char Hex[12];
                int n = 0;
                const char *P = "rx=st=";
                gRxStLogged = 1;
                while (*P) {
                    Line[n++] = *P++;
                }
                HalSerialFormatHex(Hex, (St >> 24) & 0xffu, 2);
                Line[n++] = Hex[2];
                Line[n++] = Hex[3];
                HalSerialFormatHex(Hex, (St >> 16) & 0xffu, 2);
                Line[n++] = Hex[2];
                Line[n++] = Hex[3];
                HalSerialFormatHex(Hex, (St >> 8) & 0xffu, 2);
                Line[n++] = Hex[2];
                Line[n++] = Hex[3];
                HalSerialFormatHex(Hex, St & 0xffu, 2);
                Line[n++] = Hex[2];
                Line[n++] = Hex[3];
                Line[n] = 0;
                IwlLogStage(Line);
            }
            if (gIwlWpa2Ok) {
                IwlRxDataToNet(Mutable, FLen, St);
            }
        }
        (void)Code;
    }
    }
}

int IwlGetLink(int *Up, UINT32 *Mbps, int *FullDuplex) {
    if (Up) {
        *Up = IwlAssociated() ? 1 : 0;
    }
    if (Mbps) {
        *Mbps = IwlAssociated() ? 54u : 0;
    }
    if (FullDuplex) {
        *FullDuplex = 1;
    }
    return 0;
}
