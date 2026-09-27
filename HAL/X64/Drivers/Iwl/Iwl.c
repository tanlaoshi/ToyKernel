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
UINT16 gIwlDid;
UINT8 gIwlBus;
UINT8 gIwlDev;
UINT8 gIwlFn;
UINT8 gIwlMac[6];
UINT8 gIwlBssid[6];
UINT16 gIwlAid;
UINT8 gIwlPtk[16];
UINT8 gIwlGtk[16];
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
    /* 刀 #132：#131 auth 前 Prep → auth=to。Auth 仍无 MAC；Assoc 前再 Prep */
    if (!IwlAssocRun()) {
        IwlLogStage("assoc=fail");
        return 0;
    }
    (void)IwlMacCtxtAssoc();
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
        Pn = gTxPn++;
        /* CCMP hdr @24：PN0 PN1 0 ExtIV|KeyID PN2..PN5 */
        Wlan[24] = (UINT8)Pn;
        Wlan[25] = (UINT8)(Pn >> 8);
        Wlan[26] = 0;
        Wlan[27] = 0x20; /* ExtIV，KeyID=0 */
        Wlan[28] = (UINT8)(Pn >> 16);
        Wlan[29] = (UINT8)(Pn >> 24);
        Wlan[30] = (UINT8)(Pn >> 32);
        Wlan[31] = (UINT8)(Pn >> 40);
        /* LLC/SNAP @32 */
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
        /* Encrypt：HdrLen=32（MAC+CCMP），body 自 32 起；MIC 写在 body 后 */
        if (!IwlCcmpEncrypt(gIwlPtk, Pn, Wlan, 32, BodyLen)) {
            return -1;
        }
        WireLen = 32u + BodyLen + 8u;
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
    return IwlSendFrameRaw(Wlan, WireLen);
}

static void IwlRxDataToNet(UINT8 *Frame, UINTN FLen) {
    UINT16 Fc;
    UINTN HdrLen;
    UINTN BodyOff;
    UINTN BodyLen;
    UINT8 Eth[400];
    UINTN EthLen;
    UINTN i;
    UINT64 Pn;
    int Prot;
    int Qos;
    int FromDs;
    int ToDs;

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
    HdrLen = 24u;
    if ((Fc & 0x0300u) == 0x0300u) {
        HdrLen += 6u;
    }
    if (Qos) {
        HdrLen += 2u;
    }
    if (FLen < HdrLen + 8u) {
        return;
    }
    BodyOff = HdrLen;
    BodyLen = FLen - HdrLen;
    if (Prot) {
        if (BodyLen < 8u + 8u) {
            return; /* CCMP + MIC */
        }
        /* PN from CCMP hdr */
        Pn = (UINT64)Frame[BodyOff]
           | ((UINT64)Frame[BodyOff + 1] << 8)
           | ((UINT64)Frame[BodyOff + 4] << 16)
           | ((UINT64)Frame[BodyOff + 5] << 24)
           | ((UINT64)Frame[BodyOff + 6] << 32)
           | ((UINT64)Frame[BodyOff + 7] << 40);
        BodyLen -= 8u; /* MIC */
        BodyLen -= 8u; /* leave CCMP in place; decrypt uses HdrLen+8 style */
        /*
         * Layout [hdr][ccmp8][body][mic8]. Decrypt with HdrLen'=HdrLen+8
         * so crypto starts at body; BodyLen = ciphertext len.
         */
        {
            UINTN CryptHdr = HdrLen + 8u;
            UINTN CryptBody = FLen - CryptHdr - 8u;
            if (!IwlCcmpDecrypt(gIwlPtk, Pn, Frame, CryptHdr, CryptBody)) {
                /* 尝试 GTK（广播） */
                if (!IwlCcmpDecrypt(gIwlGtk, Pn, Frame, CryptHdr, CryptBody)) {
                    return;
                }
            }
            BodyOff = CryptHdr;
            BodyLen = CryptBody;
        }
    }
    if (BodyLen < 8u) {
        return;
    }
    /* LLC/SNAP */
    if (Frame[BodyOff] != 0xAA || Frame[BodyOff + 1] != 0xAA) {
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
    EthLen = 14u + (BodyLen - 8u);
    if (EthLen > sizeof(Eth)) {
        return;
    }
    for (i = 0; i < BodyLen - 8u; i++) {
        Eth[14 + i] = Frame[BodyOff + 8 + i];
    }
    NetInputFrame(Eth, EthLen);
}

void IwlPoll(void) {
    IWL_RX_PKT *Pkt;
    UINTN Len;

    if (!gIwlAlive) {
        return;
    }
    IwlRxPoll();
    while (IwlRxTake(&Pkt, &Len)) {
        UINT8 Code;
        const UINT8 *Payload;
        UINTN PayLen;
        UINT8 Mutable[512];
        UINTN i;

        if (!Pkt || Len < sizeof(IWL_CMD_HDR)) {
            continue;
        }
        Code = Pkt->Hdr.Code;
        Payload = Pkt->Data;
        PayLen = Len - sizeof(IWL_CMD_HDR);
        if (Code == IWL_RX_MPDU_CMD && PayLen > 4) {
            UINTN FLen = PayLen - 4;
            if (FLen > sizeof(Mutable)) {
                FLen = sizeof(Mutable);
            }
            for (i = 0; i < FLen; i++) {
                Mutable[i] = Payload[4 + i];
            }
            if (gIwlWpa2Ok) {
                IwlRxDataToNet(Mutable, FLen);
            }
        }
        (void)Code;
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
