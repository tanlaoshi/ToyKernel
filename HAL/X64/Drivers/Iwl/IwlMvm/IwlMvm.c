/*
 * IwlMvm.c — scan_cfg / MCC / post_alive（PR-S-iwl-split-3）
 */
#include "IwlMvmInternal.h"
#include "HalSerial.h"

int IwlMvmScanCfg(void) {
    /*
     * 刀 #70：#69 证 scfg TX 通、0xC 要靠 ant3 撞出，撞出后 ant3 卡死。
     * scfg∥ant3 Kick → 等 0x0C → Unwedge 推 RDPTR → ant4 金丝雀。
     */
    UINT8 Buf[36 + IWL_SCAN_NCHAN_CAPA];
    UINT8 Kick[4];
    UINT32 Rates = IWL_SCAN_CFG_RATE_1M | IWL_SCAN_CFG_RATE_2M
                 | IWL_SCAN_CFG_RATE_5M | IWL_SCAN_CFG_RATE_11M
                 | IWL_SCAN_CFG_RATE_6M | IWL_SCAN_CFG_RATE_9M
                 | IWL_SCAN_CFG_RATE_12M | IWL_SCAN_CFG_RATE_18M
                 | IWL_SCAN_CFG_RATE_24M | IWL_SCAN_CFG_RATE_36M
                 | IWL_SCAN_CFG_RATE_48M | IWL_SCAN_CFG_RATE_54M;
    UINT32 Flags;
    UINT32 i;
    UINT8 Chans[] = {
        1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13
    };
    UINT32 Nchan = 13;
    int GotScfg = 0;

    IwlLogVerb("mvm=v83");
    IwlMvmZero(Buf, sizeof(Buf));
    Flags = IWL_SCAN_CFG_FLAG_ACTIVATE
          | IWL_SCAN_CFG_FLAG_ALLOW_CHUB
          | IWL_SCAN_CFG_FLAG_SET_TX_CHAINS
          | IWL_SCAN_CFG_FLAG_SET_RX_CHAINS
          | IWL_SCAN_CFG_FLAG_SET_AUX_STA_ID
          | IWL_SCAN_CFG_FLAG_SET_ALL_TIMES
          | IWL_SCAN_CFG_FLAG_SET_LEGACY_RATES
          | IWL_SCAN_CFG_FLAG_SET_MAC_ADDR
          | IWL_SCAN_CFG_FLAG_SET_CHANNEL_FLAGS
          | IWL_SCAN_CFG_FLAG_CLEAR_FRAGMENTED
          | IWL_SCAN_CFG_N_CHANNELS(Nchan);
    IwlMvmPut32(Buf + 0, Flags);
    IwlMvmPut32(Buf + 4, IWL_ANT_AB);
    IwlMvmPut32(Buf + 8, IWL_ANT_AB);
    Rates |= IWL_SCAN_CFG_SUPPORTED_RATE(Rates);
    IwlMvmPut32(Buf + 12, Rates);
    Buf[24] = 10;
    Buf[25] = 110;
    Buf[26] = 44;
    Buf[27] = 90;
    IwlMvmCopy(Buf + 28, gIwlMac, 6);
    Buf[34] = (UINT8)IWL_AUX_STA_ID;
    Buf[35] = 0;
    for (i = 0; i < Nchan; i++) {
        Buf[36 + i] = Chans[i];
    }
    if (IwlSendCmd(IWL_CMD_ID(IWL_CMD_SCAN_CFG, IWL_LONG_GROUP, 0),
                   Buf, (UINT32)sizeof(Buf), -1) != 0) {
        IwlMvmLogCmdFail("mvm=scfg", IWL_CMD_SCAN_CFG);
        return 0;
    }
    IwlMvmZero(Kick, sizeof(Kick));
    Kick[0] = (UINT8)IWL_ANT_AB;
    Kick[1] = (UINT8)(IWL_ANT_AB >> 8);
    Kick[2] = (UINT8)(IWL_ANT_AB >> 16);
    Kick[3] = (UINT8)(IWL_ANT_AB >> 24);
    if (IwlSendCmd(IWL_CMD_TX_ANT_CFG, Kick, sizeof(Kick), -1) != 0) {
        IwlLogStage("ant3=qfail");
    }
    IwlCmdKick();
    IwlLogVerb("scfg=kick");

    for (i = 0; i < 2500; i++) {
        IWL_RX_PKT *Pkt;
        UINTN Len;
        IwlRxPoll();
        while (IwlRxTake(&Pkt, &Len)) {
            if (!(Pkt->Hdr.Qid & 0x80u) && Pkt->Hdr.Code == IWL_CMD_SCAN_CFG) {
                GotScfg = 1;
            }
            if (IwlRspStashClaim(IWL_CMD_SCAN_CFG, 0xff)) {
                GotScfg = 1;
            }
        }
        if (IwlRspStashClaim(IWL_CMD_SCAN_CFG, 0xff)) {
            GotScfg = 1;
        }
        if (GotScfg) {
            break;
        }
        IwlStallMs(1);
    }
    if (GotScfg) { IwlLogVerb("scfg=got"); } else { IwlLogStage("scfg=miss"); }
    IwlCmdqSnap();

    IwlCmdqUnwedge();

    IwlMvmZero(Kick, sizeof(Kick));
    Kick[0] = (UINT8)IWL_ANT_AB;
    Kick[1] = (UINT8)(IWL_ANT_AB >> 8);
    Kick[2] = (UINT8)(IWL_ANT_AB >> 16);
    Kick[3] = (UINT8)(IWL_ANT_AB >> 24);
    if (IwlSendCmd(IWL_CMD_TX_ANT_CFG, Kick, sizeof(Kick), 1) == 0) {
        IwlLogVerb("ant4=ok");
    } else {
        IwlLogStage("ant4=fail");
        IwlCmdqSnap();
    }
    return 1;
}

/* 刀 #76：#75 mcc=ok 但仍无扫；OpenBSD 是 MCC→SCAN_CFG，我们曾 scfg 在前 */
int IwlMvmMcc(void) {
    UINT8 Mcc[IWL_MCC_UPDATE_SIZE];

    IwlMvmZero(Mcc, sizeof(Mcc));
    Mcc[0] = (UINT8)'Z';
    Mcc[1] = (UINT8)'Z';
    Mcc[2] = (UINT8)IWL_MCC_SOURCE_GET_CURRENT;
    if (IwlSendCmd(IWL_CMD_MCC_UPDATE, Mcc, sizeof(Mcc), 1) == 0) {
        IwlLogVerb("mcc=ok");
        return 1;
    }
    IwlLogStage("mcc=fail");
    IwlCmdqSnap();
    IwlCmdqUnwedge();
    return 0;
}

/* 刀 #38：跳过残缺 PHY_CONFIG；TX_ANT×2 验证「多命令」是否与 phy 无关 */
int IwlMvmAntCanary(void) {
    UINT8 Cmd[4];
    UINT32 Ant = IWL_ANT_AB;

    IwlMvmZero(Cmd, sizeof(Cmd));
    Cmd[0] = (UINT8)Ant;
    Cmd[1] = (UINT8)(Ant >> 8);
    Cmd[2] = (UINT8)(Ant >> 16);
    Cmd[3] = (UINT8)(Ant >> 24);
    if (IwlSendCmd(IWL_CMD_TX_ANT_CFG, Cmd, sizeof(Cmd), 1) != 0) {
        IwlMvmLogCmdFail("ant1", IWL_CMD_TX_ANT_CFG);
        return 0;
    }
    IwlLogVerb("ant1=ok");
    if (IwlSendCmd(IWL_CMD_TX_ANT_CFG, Cmd, sizeof(Cmd), 1) != 0) {
        IwlMvmLogCmdFail("ant2", IWL_CMD_TX_ANT_CFG);
        return 0;
    }
    IwlLogVerb("ant2=ok");
    return 1;
}

int IwlMvmPostAlive(void) {
    IwlRxDrain();
    /*
     * 刀 #76：phy→aux→MCC→scfg∥ant3→unwedge→ant4（OpenBSD：LAR MCC 在 scfg 前）。
     */
    IwlLogVerb("mvm=v83");
    if (!IwlPhyDbSend()) {
    }
    if (!IwlPhyCfgRt()) {
    }
    if (!IwlMvmAntCanary()) {
    }
    if (!IwlMvmDqaEnable()) {
    }
    if (!IwlMvmPhyCtxt()) {
    }
    if (!IwlMvmAuxSta()) {
    }
    if (!IwlMvmMcc()) {
    }
    if (!IwlMvmScanCfg()) {
        return 0;
    }
    IwlLogStage("mvm=ok");
    return 1;
}
