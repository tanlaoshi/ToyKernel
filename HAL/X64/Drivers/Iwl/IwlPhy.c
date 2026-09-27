/*
 * IwlPhy.c — INIT 校准 → phy_db → PHY_CONFIG（刀 #57/58）
 *
 * OpenBSD：INIT 上 TX_ANT+PHY_CFG → 收 CALIB_RES；RT 上先 phy_db 再 PHY_CFG。
 * #57：CFG+NCH 通但 scfg 仍毒 FH。#58：收齐 PAPD/TXP 频道组再下发。
 */
#include "IwlPrivate.h"
#include "HalSerial.h"

#define IWL_PHY_CFG_MAX     512u
#define IWL_PHY_NCH_MAX     2048u
#define IWL_PHY_CHG_MAX     4096u /* #59：TXP=0x808 PAPD=0x610，旧 1024 全丢 */
#define IWL_PHY_SEC_MAX     4096u
#define IWL_NUM_CH_GROUPS   9u

static UINT8 gPhyCfgData[IWL_PHY_CFG_MAX];
static UINT16 gPhyCfgLen;
static UINT8 gPhyNchData[IWL_PHY_NCH_MAX];
static UINT16 gPhyNchLen;
static UINT8 gPapdData[IWL_NUM_CH_GROUPS][IWL_PHY_CHG_MAX];
static UINT16 gPapdLen[IWL_NUM_CH_GROUPS];
static UINT8 gTxpData[IWL_NUM_CH_GROUPS][IWL_PHY_CHG_MAX];
static UINT16 gTxpLen[IWL_NUM_CH_GROUPS];
static UINT32 gPhyNotifN;
static UINT32 gPapdN;
static UINT32 gTxpN;
static UINT32 gPhyDropN; /* 超长丢弃计数 */

static void IwlZero(void *P, UINTN N) {
    UINT8 *B = (UINT8 *)P;
    UINTN i;
    for (i = 0; i < N; i++) {
        B[i] = 0;
    }
}

static void IwlCopy(void *D, const void *S, UINTN N) {
    UINT8 *d = (UINT8 *)D;
    const UINT8 *s = (const UINT8 *)S;
    UINTN i;
    for (i = 0; i < N; i++) {
        d[i] = s[i];
    }
}

static void IwlPut32(UINT8 *P, UINT32 V) {
    P[0] = (UINT8)V;
    P[1] = (UINT8)(V >> 8);
    P[2] = (UINT8)(V >> 16);
    P[3] = (UINT8)(V >> 24);
}

static void IwlPut16(UINT8 *P, UINT16 V) {
    P[0] = (UINT8)V;
    P[1] = (UINT8)(V >> 8);
}

static int IwlPhySendCfg(UINT32 UcodeType) {
    UINT8 Cmd[12];
    UINT32 Flow = gIwlCalibFlow[UcodeType];
    UINT32 Event = gIwlCalibEvent[UcodeType];

    IwlZero(Cmd, sizeof(Cmd));
    IwlPut32(Cmd, gIwlPhyCfg);
    IwlPut32(Cmd + 4, Flow);
    IwlPut32(Cmd + 8, Event);
    if (IwlSendCmd(IWL_CMD_PHY_CONFIG, Cmd, sizeof(Cmd), 1) != 0) {
        return 0;
    }
    return 1;
}

static int IwlPhySendAnt(void) {
    UINT8 Cmd[4];
    UINT32 Ant = IWL_ANT_AB;

    IwlZero(Cmd, sizeof(Cmd));
    IwlPut32(Cmd, Ant);
    return IwlSendCmd(IWL_CMD_TX_ANT_CFG, Cmd, sizeof(Cmd), 1) == 0;
}

static UINT8 gCalTypes[12];
static UINT16 gCalLens[12];
static UINT32 gCalDumpN;

static void IwlPhyStoreSection(UINT16 Type, const UINT8 *Data, UINT16 Len) {
    UINT16 Chg;

    if (gCalDumpN < 12) {
        gCalTypes[gCalDumpN] = (UINT8)Type;
        gCalLens[gCalDumpN] = Len;
        gCalDumpN++;
    }
    if (Type == IWL_PHY_DB_CFG && Len <= IWL_PHY_CFG_MAX) {
        IwlCopy(gPhyCfgData, Data, Len);
        gPhyCfgLen = Len;
    } else if (Type == IWL_PHY_DB_CALIB_NCH && Len <= IWL_PHY_NCH_MAX) {
        IwlCopy(gPhyNchData, Data, Len);
        gPhyNchLen = Len;
    } else if (Type == IWL_PHY_DB_CALIB_CHG_PAPD && Len >= 2) {
        if (Len > IWL_PHY_CHG_MAX) {
            gPhyDropN++;
            return;
        }
        Chg = (UINT16)Data[0] | ((UINT16)Data[1] << 8);
        if (Chg < IWL_NUM_CH_GROUPS) {
            IwlCopy(gPapdData[Chg], Data, Len);
            gPapdLen[Chg] = Len;
            gPapdN++;
        }
    } else if (Type == IWL_PHY_DB_CALIB_CHG_TXP && Len >= 2) {
        if (Len > IWL_PHY_CHG_MAX) {
            gPhyDropN++;
            return;
        }
        Chg = (UINT16)Data[0] | ((UINT16)Data[1] << 8);
        if (Chg < IWL_NUM_CH_GROUPS) {
            IwlCopy(gTxpData[Chg], Data, Len);
            gTxpLen[Chg] = Len;
            gTxpN++;
        }
    }
}

static void IwlPhyHandleRx(IWL_RX_PKT *Pkt, UINTN Len, int *GotInit) {
    UINT8 Code = Pkt->Hdr.Code;
    UINTN HdrSz = Pkt->Hdr.Flags ? sizeof(IWL_CMD_HDR_WIDE)
                                  : sizeof(IWL_CMD_HDR);
    const UINT8 *Pay = (const UINT8 *)&Pkt->Hdr + HdrSz;
    UINTN PayLen = Len > HdrSz ? Len - HdrSz : 0;

    if (Code == IWL_INIT_COMPLETE_NOTIF) {
        *GotInit = 1;
        return;
    }
    if (Code != IWL_CALIB_RES_NOTIF_PHY_DB || PayLen < 4) {
        return;
    }
    {
        UINT16 Type = (UINT16)Pay[0] | ((UINT16)Pay[1] << 8);
        UINT16 Sz = (UINT16)Pay[2] | ((UINT16)Pay[3] << 8);
        if (4u + Sz > PayLen) {
            return;
        }
        gPhyNotifN++;
        IwlPhyStoreSection(Type, Pay + 4, Sz);
    }
}

int IwlPhyInitCalib(void) {
    UINT32 i;
    UINT32 Idle = 0;
    IWL_RX_PKT *Pkt;
    UINTN Len;
    int GotInit = 0;
    char Line[40];
    char Hex[12];
    int n = 0;
    const char *P;
    UINT32 g;

    gPhyCfgLen = 0;
    gPhyNchLen = 0;
    gPhyNotifN = 0;
    gPapdN = 0;
    gTxpN = 0;
    gPhyDropN = 0;
    gCalDumpN = 0;
    for (g = 0; g < IWL_NUM_CH_GROUPS; g++) {
        gPapdLen[g] = 0;
        gTxpLen[g] = 0;
    }

    if (!IwlPhySendAnt()) {
        IwlLogStage("initant=fail");
    }
    if (!IwlPhySendCfg(IWL_UCODE_TYPE_INIT)) {
        IwlLogStage("initphy=fail");
        return 0;
    }
    IwlLogVerb("initphy=ok");

    /*
     * 刀 #58：勿在首个 CFG+NCH 就停；等 INIT_COMPLETE 后再静默 80ms，
     * 以收齐 PAPD/TXP 频道组（#57 的 h=0004 且 scfg 仍毒）。
     */
    for (i = 0; i < 3000; i++) {
        int Hit = 0;
        IwlRxPoll();
        while (IwlRxTake(&Pkt, &Len)) {
            IwlPhyHandleRx(Pkt, Len, &GotInit);
            Hit = 1;
            Idle = 0;
        }
        if (!Hit) {
            Idle++;
        }
        if (GotInit && gPhyCfgLen && Idle >= 80u) {
            break;
        }
        IwlStallMs(1);
    }

    P = "initcal n=";
    while (*P) {
        Line[n++] = *P++;
    }
    HalSerialFormatHex(Hex, gPhyNotifN, 2);
    Line[n++] = Hex[2];
    Line[n++] = Hex[3];
    Line[n++] = ' ';
    Line[n++] = 'c';
    Line[n++] = '=';
    HalSerialFormatHex(Hex, gPhyCfgLen, 4);
    Line[n++] = Hex[2];
    Line[n++] = Hex[3];
    Line[n++] = Hex[4];
    Line[n++] = Hex[5];
    Line[n++] = ' ';
    Line[n++] = 'h';
    Line[n++] = '=';
    HalSerialFormatHex(Hex, gPhyNchLen, 4);
    Line[n++] = Hex[2];
    Line[n++] = Hex[3];
    Line[n++] = Hex[4];
    Line[n++] = Hex[5];
    Line[n] = 0;
    IwlLogVerb(Line);

    n = 0;
    P = "initgrp p=";
    while (*P) {
        Line[n++] = *P++;
    }
    HalSerialFormatHex(Hex, gPapdN, 2);
    Line[n++] = Hex[2];
    Line[n++] = Hex[3];
    Line[n++] = ' ';
    Line[n++] = 't';
    Line[n++] = '=';
    HalSerialFormatHex(Hex, gTxpN, 2);
    Line[n++] = Hex[2];
    Line[n++] = Hex[3];
    Line[n++] = ' ';
    Line[n++] = 'd';
    Line[n++] = '=';
    HalSerialFormatHex(Hex, gPhyDropN, 2);
    Line[n++] = Hex[2];
    Line[n++] = Hex[3];
    Line[n] = 0;
    IwlLogVerb(Line);

    /* 刀 #59 诊断保留首 4 条；#60 重点看 p/t */
    return (gPhyCfgLen > 0 && gPhyNchLen > 0) ? 1 : 0;
}

static int IwlPhyDbSendOne(UINT16 Type, const UINT8 *Data, UINT16 Len) {
    static UINT8 Buf[4 + IWL_PHY_SEC_MAX];

    if (Len == 0 || Len > IWL_PHY_SEC_MAX) {
        return 0;
    }
    IwlPut16(Buf, Type);
    IwlPut16(Buf + 2, Len);
    IwlCopy(Buf + 4, Data, Len);
    return IwlSendCmd(IWL_CMD_PHY_DB, Buf, 4u + Len, 1) == 0;
}

int IwlPhyDbSend(void) {
    UINT32 g;
    UINT32 Sent = 0;

    if (!gPhyCfgLen || !gPhyNchLen) {
        IwlLogVerb("phydb=empty");
        return 0;
    }
    if (!IwlPhyDbSendOne(IWL_PHY_DB_CFG, gPhyCfgData, gPhyCfgLen)) {
        IwlLogVerb("phydb=cfg");
        return 0;
    }
    if (!IwlPhyDbSendOne(IWL_PHY_DB_CALIB_NCH, gPhyNchData, gPhyNchLen)) {
        IwlLogVerb("phydb=nch");
        return 0;
    }
    for (g = 0; g < IWL_NUM_CH_GROUPS; g++) {
        if (gPapdLen[g] == 0) {
            continue;
        }
        if (!IwlPhyDbSendOne(IWL_PHY_DB_CALIB_CHG_PAPD, gPapdData[g], gPapdLen[g])) {
            IwlLogVerb("phydb=papd");
            return 0;
        }
        Sent++;
        IwlStallMs(1);
    }
    for (g = 0; g < IWL_NUM_CH_GROUPS; g++) {
        if (gTxpLen[g] == 0) {
            continue;
        }
        if (!IwlPhyDbSendOne(IWL_PHY_DB_CALIB_CHG_TXP, gTxpData[g], gTxpLen[g])) {
            IwlLogVerb("phydb=txp");
            return 0;
        }
        Sent++;
        IwlStallMs(1);
    }
    {
        char Line[24];
        char Hex[12];
        int n = 0;
        const char *P = "phydb=ok g=";
        while (*P) {
            Line[n++] = *P++;
        }
        HalSerialFormatHex(Hex, Sent, 2);
        Line[n++] = Hex[2];
        Line[n++] = Hex[3];
        Line[n] = 0;
        IwlLogVerb(Line);
    }
    return 1;
}

int IwlPhyCfgRt(void) {
    if (!IwlPhySendCfg(IWL_UCODE_TYPE_REGULAR)) {
        IwlLogStage("phyrt=fail");
        return 0;
    }
    IwlLogVerb("phyrt=ok");
    return 1;
}
