/*
 * IwlPhyCalib.c — INIT 校准收集（PR-S-iwl-split-5）
 */
#include "IwlPhyInternal.h"
#include "HalSerial.h"

UINT8 gCalTypes[12];
UINT16 gCalLens[12];
UINT32 gCalDumpN;

void IwlPhyStoreSection(UINT16 Type, const UINT8 *Data, UINT16 Len) {
    UINT16 Chg;

    if (gCalDumpN < 12) {
        gCalTypes[gCalDumpN] = (UINT8)Type;
        gCalLens[gCalDumpN] = Len;
        gCalDumpN++;
    }
    if (Type == IWL_PHY_DB_CFG && Len <= IWL_PHY_CFG_MAX) {
        IwlPhyCopy(gPhyCfgData, Data, Len);
        gPhyCfgLen = Len;
    } else if (Type == IWL_PHY_DB_CALIB_NCH && Len <= IWL_PHY_NCH_MAX) {
        IwlPhyCopy(gPhyNchData, Data, Len);
        gPhyNchLen = Len;
    } else if (Type == IWL_PHY_DB_CALIB_CHG_PAPD && Len >= 2) {
        if (Len > IWL_PHY_CHG_MAX) {
            gPhyDropN++;
            return;
        }
        Chg = (UINT16)Data[0] | ((UINT16)Data[1] << 8);
        if (Chg < IWL_NUM_CH_GROUPS) {
            IwlPhyCopy(gPapdData[Chg], Data, Len);
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
            IwlPhyCopy(gTxpData[Chg], Data, Len);
            gTxpLen[Chg] = Len;
            gTxpN++;
        }
    }
}

void IwlPhyHandleRx(IWL_RX_PKT *Pkt, UINTN Len, int *GotInit) {
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

