/*
 * IwlPhyDb.c — phy_db 下发（PR-S-iwl-split-5）
 */
#include "IwlPhyInternal.h"
#include "HalSerial.h"

int IwlPhyDbSendOne(UINT16 Type, const UINT8 *Data, UINT16 Len) {
    static UINT8 Buf[4 + IWL_PHY_SEC_MAX];

    if (Len == 0 || Len > IWL_PHY_SEC_MAX) {
        return 0;
    }
    IwlPhyPut16(Buf, Type);
    IwlPhyPut16(Buf + 2, Len);
    IwlPhyCopy(Buf + 4, Data, Len);
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

