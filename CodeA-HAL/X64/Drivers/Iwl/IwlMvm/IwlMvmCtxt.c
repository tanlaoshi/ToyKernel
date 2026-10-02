/*
 * IwlMvmCtxt.c — PHY/MAC/STA/binding（PR-S-iwl-split-3）
 */
#include "IwlMvmInternal.h"
#include "HalSerial.h"

/*
 * 刀 #64：#63 证等 FH idle 无效（scfg=to，ACK 仍被 scan 撞出）。
 * OpenBSD 在 scfg 前有 phy_ctxt ADD；补最小 2.4G ch1 上下文。
 */
int IwlMvmPhyCtxt(void) {
    UINT8 Cmd[36];
    UINT32 RxChain;
    UINT32 Ant = IWL_ANT_AB;
    UINT32 Nant = 2; /* A+B */

    IwlMvmZero(Cmd, sizeof(Cmd));
    IwlMvmPut32(Cmd + 0, 0);
    IwlMvmPut32(Cmd + 4, IWL_FW_CTXT_ACTION_ADD);
    Cmd[16] = (UINT8)IWL_PHY_BAND_24;
    Cmd[17] = 1;
    Cmd[18] = (UINT8)IWL_PHY_VHT_CHANNEL_MODE20;
    Cmd[19] = (UINT8)IWL_PHY_VHT_CTRL_POS_1_BELOW;
    IwlMvmPut32(Cmd + 20, Ant);
    RxChain = (Ant << IWL_PHY_RX_CHAIN_VALID_POS)
            | (Nant << IWL_PHY_RX_CHAIN_CNT_POS)
            | (Nant << IWL_PHY_RX_CHAIN_MIMO_CNT_POS);
    IwlMvmPut32(Cmd + 24, RxChain);
    if (IwlSendCmd(IWL_CMD_PHY_CONTEXT, Cmd, sizeof(Cmd), 1) != 0) {
        IwlMvmLogCmdFail("phyctx", IWL_CMD_PHY_CONTEXT);
        return 0;
    }
    IwlLogVerb("phyctx=ok");
    return 1;
}

/*
 * 刀 #66：#65 无 scfg 时 scan FH 通但 RX c=FF（拒绝）。
 * #61 PRPH-auxq+ADD_STA 毒队列；OpenBSD 是 SCD_QUEUE_CFG(0x1d) 后再 ADD_STA。
 */
int IwlMvmAuxSta(void) {
    UINT8 Qcfg[IWL_SCD_TXQ_CFG_CMD_SIZE];
    UINT8 Sta[IWL_ADD_STA_CMD_SIZE];
    UINT32 Qid = IWL_DQA_AUX_QUEUE;
    UINT32 MacColor = IWL_FW_CMD_ID_AND_COLOR(IWL_MAC_INDEX_AUX, 0);

    if (IwlNicLock()) {
        IwlMmioW32(IWL_HBUS_TARG_WRPTR, (Qid << 8) | 0);
        IwlNicUnlock();
    }
    IwlMvmZero(Qcfg, sizeof(Qcfg));
    Qcfg[0] = 0;
    Qcfg[1] = (UINT8)IWL_AUX_STA_ID;
    Qcfg[2] = (UINT8)IWL_MAX_TID_COUNT;
    Qcfg[3] = (UINT8)Qid;
    Qcfg[4] = 1;
    Qcfg[5] = 0;
    Qcfg[6] = (UINT8)IWL_TX_FIFO_MCAST;
    Qcfg[7] = (UINT8)IWL_FRAME_LIMIT;
    IwlMvmPut16(Qcfg + 8, 0);
    if (IwlSendCmd(IWL_CMD_SCD_QUEUE_CFG, Qcfg, sizeof(Qcfg), 1) != 0) {
        IwlMvmLogCmdFail("auxq", IWL_CMD_SCD_QUEUE_CFG);
        return 0;
    }
    IwlLogVerb("auxq=ok");

    IwlMvmZero(Sta, sizeof(Sta));
    IwlMvmPut16(Sta + 2, 0xffff);
    IwlMvmPut32(Sta + 4, MacColor);
    Sta[16] = (UINT8)IWL_AUX_STA_ID;
    Sta[35] = (UINT8)IWL_STA_AUX_ACTIVITY;
    IwlMvmPut32(Sta + 40, 1u << Qid);
    if (IwlSendCmd(IWL_CMD_ADD_STA, Sta, sizeof(Sta), 1) != 0) {
        IwlMvmLogCmdFail("auxsta", IWL_CMD_ADD_STA);
        return 0;
    }
    IwlLogVerb("auxsta=ok");
    if (!IwlEnableAuxTxq()) {
        IwlLogStage("auxhw=fail");
        return 0;
    }
    return 1;
}

/*
 * 刀 #125：#124 改 BSS_CLIENT q4 → 又 cmdto o=18 / n=00。
 * 回退 #123 已过的 **仅 q5** v10 ADD_STA；保留 gIwlTxStaId=AP + macmod。
 * 刀 #128：SCD 仍 MCAST（#127 改 VO 未达）；EnableApTxq 挪到 macmod 后。
 */
int IwlAddApSta(void) {
    UINT8 Qcfg[IWL_SCD_TXQ_CFG_CMD_SIZE];
    UINT8 Sta[IWL_ADD_STA_CMD_SIZE];
    UINT32 MacColor = IWL_FW_CMD_ID_AND_COLOR(0, 0);
    UINT32 Qid = IWL_DQA_BSS_CLIENT_QUEUE;
    UINT32 Flg = IWL_STA_FLG_CLASS_AUTH | IWL_STA_FLG_CLASS_ASSOC;

    if (!IwlPrepareApTxq()) {
        IwlLogStage("apq=nobind");
        return 0;
    }
    IwlMvmZero(Qcfg, sizeof(Qcfg));
    Qcfg[0] = 0;
    Qcfg[1] = (UINT8)IWL_AP_STA_ID;
    Qcfg[2] = (UINT8)IWL_MAX_TID_COUNT;
    Qcfg[3] = (UINT8)Qid;
    Qcfg[4] = 1;
    Qcfg[5] = 0;
    Qcfg[6] = (UINT8)IWL_TX_FIFO_VO;
    Qcfg[7] = (UINT8)IWL_FRAME_LIMIT;
    IwlMvmPut16(Qcfg + 8, 0);
    IwlLogStage("apq=tx");
    if (IwlSendCmd(IWL_CMD_SCD_QUEUE_CFG, Qcfg, sizeof(Qcfg), 150) != 0) {
        IwlMvmLogCmdFail("apq", IWL_CMD_SCD_QUEUE_CFG);
        (void)IwlCmdqUnwedge();
        return 0;
    }
    IwlLogStage("apq=ok");

    IwlMvmZero(Sta, sizeof(Sta));
    IwlMvmPut16(Sta + 2, 0xffff);
    IwlMvmPut32(Sta + 4, MacColor);
    IwlMvmCopy(Sta + 8, gIwlBssid, 6);
    Sta[16] = (UINT8)IWL_AP_STA_ID;
    IwlMvmPut32(Sta + 20, Flg);
    IwlMvmPut32(Sta + 24, Flg);
    Sta[35] = (UINT8)IWL_STA_LINK;
    IwlMvmPut16(Sta + 36, gIwlAid ? gIwlAid : 1u);
    IwlMvmPut32(Sta + 40, 1u << Qid);
    IwlLogStage("apsta=tx");
    if (IwlSendCmd(IWL_CMD_ADD_STA, Sta, sizeof(Sta), 200) != 0) {
        IwlMvmLogCmdFail("apsta", IWL_CMD_ADD_STA);
        (void)IwlCmdqUnwedge();
        return 0;
    }
    gIwlTxStaId = (UINT8)IWL_AP_STA_ID;
    IwlAuxTxLogReset();
    IwlLogStage("apsta=ok");
    return 1;
}

/* 刀 #89：phy 从扫到的信道 MODIFY（旧死钉 ch1 → 收不到他信道 EAPOL） */
int IwlPhyCtxtTune(UINT8 Chan) {
    UINT8 Cmd[36];
    UINT32 RxChain;
    UINT32 Ant = IWL_ANT_AB;
    UINT32 Nant = 2;
    char Line[20];
    char Hex[12];
    int n = 0;
    const char *P = "phy=ch";

    if (Chan == 0 || Chan > 14) {
        Chan = 1;
    }
    IwlMvmZero(Cmd, sizeof(Cmd));
    IwlMvmPut32(Cmd + 0, 0);
    IwlMvmPut32(Cmd + 4, IWL_FW_CTXT_ACTION_MODIFY);
    Cmd[16] = (UINT8)IWL_PHY_BAND_24;
    Cmd[17] = Chan;
    Cmd[18] = (UINT8)IWL_PHY_VHT_CHANNEL_MODE20;
    Cmd[19] = (UINT8)IWL_PHY_VHT_CTRL_POS_1_BELOW;
    IwlMvmPut32(Cmd + 20, Ant);
    RxChain = (Ant << IWL_PHY_RX_CHAIN_VALID_POS)
            | (Nant << IWL_PHY_RX_CHAIN_CNT_POS)
            | (Nant << IWL_PHY_RX_CHAIN_MIMO_CNT_POS);
    IwlMvmPut32(Cmd + 24, RxChain);
    while (*P) {
        Line[n++] = *P++;
    }
    HalSerialFormatHex(Hex, Chan, 2);
    Line[n++] = Hex[2];
    Line[n++] = Hex[3];
    Line[n] = 0;
    if (IwlSendCmd(IWL_CMD_PHY_CONTEXT, Cmd, sizeof(Cmd), 1) != 0) {
        IwlMvmLogCmdFail("phytune", IWL_CMD_PHY_CONTEXT);
        return 0;
    }
    IwlLogVerb(Line);
    return 1;
}

/*
 * 刀 #105/#106：BSS_STA。Prep 用 is_assoc=0；Assoc 后 MODIFY=1。
 * 刀 #180/#181：FwDecrypt — 0=全禁；1=单+组；2=仅单播（组仍 DIS_GRP，主机解）。
 */
int IwlMacCtxtSend(UINT32 Action, int IsAssoc, int Sync, int FwDecrypt) {
    UINT8 Cmd[IWL_MAC_CTX_CMD_SIZE];
    UINT32 MacId = IWL_FW_CMD_ID_AND_COLOR(0, 0);
    UINT32 Filter;
    UINTN i;
    UINT8 *Ac;

    IwlMvmZero(Cmd, sizeof(Cmd));
    IwlMvmPut32(Cmd + 0, MacId);
    IwlMvmPut32(Cmd + 4, Action);
    IwlMvmPut32(Cmd + 8, IWL_FW_MAC_TYPE_BSS_STA);
    IwlMvmPut32(Cmd + 12, IWL_TSF_ID_A);
    IwlMvmCopy(Cmd + 16, gIwlMac, 6);
    IwlMvmCopy(Cmd + 24, gIwlBssid, 6);
    IwlMvmPut32(Cmd + 32, 0x0fu);
    IwlMvmPut32(Cmd + 36, 0xff0u);
    IwlMvmPut32(Cmd + 44, IWL_MAC_FLG_SHORT_PREAMBLE);
    IwlMvmPut32(Cmd + 48, IWL_MAC_FLG_SHORT_SLOT);
    Filter = IWL_MAC_FILTER_ACCEPT_GRP
           | IWL_MAC_FILTER_IN_CONTROL_AND_MGMT
           | IWL_MAC_FILTER_IN_PROMISC
           | IWL_MAC_FILTER_IN_BEACON; /* #120：assoc 后仍收 beacon；#119 去掉后 n=01 */
    if (FwDecrypt == 0) {
        Filter |= IWL_MAC_FILTER_DIS_DECRYPT | IWL_MAC_FILTER_DIS_GRP_DECRYPT;
    } else if (FwDecrypt == 2) {
        Filter |= IWL_MAC_FILTER_DIS_GRP_DECRYPT; /* 刀 #181 */
    }
    IwlMvmPut32(Cmd + 52, Filter);
    Ac = Cmd + 60;
    for (i = 0; i < 5; i++) {
        Ac[i * 8 + 0] = 0x0f;
        Ac[i * 8 + 2] = 0x3f;
        Ac[i * 8 + 4] = 1;
        Ac[i * 8 + 5] = (UINT8)(1u << (i < 4 ? i : 0));
    }
    if (IsAssoc) {
        /* Linux iwl_mac_data_sta @100：勿写错 bi/AID 偏移（#120 写到 dtim_tsf 致 cmdto） */
        IwlMvmPut32(Cmd + 100, 1);                         /* is_assoc */
        IwlMvmPut32(Cmd + 104, 0);                         /* dtim_time */
        /* 108..115 dtim_tsf = 0 */
        IwlMvmPut32(Cmd + 116, 100);                       /* bi TU */
        IwlMvmPut32(Cmd + 120, 0);                         /* reserved1 */
        IwlMvmPut32(Cmd + 124, 100);                       /* dtim_interval */
        IwlMvmPut32(Cmd + 128, 0);                         /* data_policy */
        IwlMvmPut32(Cmd + 132, 10);                        /* listen_interval */
        IwlMvmPut32(Cmd + 136, gIwlAid ? (UINT32)gIwlAid : 1u); /* assoc_id */
        IwlMvmPut32(Cmd + 140, 0);                         /* assoc_beacon_arrive_time */
    }
    if (IwlSendCmd(IWL_CMD_MAC_CONTEXT, Cmd, sizeof(Cmd), Sync) != 0) {
        return 0;
    }
    return 1;
}

/*
 * 刀 #108：#107 te=600TU≈0.6s，auth 等 1.5s → TE 先结束，后两次 auth n=00。
 * 拉长到 12000TU（~12s）盖住 3 次 auth+assoc；供每轮续订。
 */
void IwlProtectSession(void) {
    UINT8 Cmd[IWL_TIME_EVENT_CMD_SIZE];
    UINT32 MacId = IWL_FW_CMD_ID_AND_COLOR(0, 0);
    UINT16 Policy;

    IwlMvmZero(Cmd, sizeof(Cmd));
    IwlMvmPut32(Cmd + 0, MacId);
    IwlMvmPut32(Cmd + 4, IWL_FW_CTXT_ACTION_ADD);
    IwlMvmPut32(Cmd + 8, IWL_TE_BSS_STA_AGGRESSIVE_ASSOC);
    IwlMvmPut32(Cmd + 16, 500);   /* max_delay */
    IwlMvmPut32(Cmd + 24, 1);
    IwlMvmPut32(Cmd + 28, 12000); /* ~12s */
    Cmd[32] = 1;
    Cmd[33] = (UINT8)IWL_TE_V2_FRAG_NONE;
    Policy = (UINT16)(IWL_TE_V2_NOTIF_HOST_EVENT_START
                     | IWL_TE_V2_NOTIF_HOST_EVENT_END
                     | IWL_TE_V2_START_IMMEDIATELY);
    IwlMvmPut16(Cmd + 34, Policy);
    if (IwlSendCmd(IWL_CMD_TIME_EVENT, Cmd, sizeof(Cmd), 200) != 0) {
        IwlMvmLogCmdFail("te", IWL_CMD_TIME_EVENT);
        (void)IwlCmdqUnwedge();
        return;
    }
    IwlLogStage("te=ok");
}

int IwlBindingAdd(void) {
    UINT8 Bind[IWL_BINDING_CMD_V1_SIZE];
    UINT32 MacId = IWL_FW_CMD_ID_AND_COLOR(0, 0);
    UINT32 PhyId = IWL_FW_CMD_ID_AND_COLOR(0, 0);

    IwlMvmZero(Bind, sizeof(Bind));
    IwlMvmPut32(Bind + 0, PhyId);
    IwlMvmPut32(Bind + 4, IWL_FW_CTXT_ACTION_ADD);
    IwlMvmPut32(Bind + 8, MacId);
    IwlMvmPut32(Bind + 12, IWL_FW_CTXT_INVALID);
    IwlMvmPut32(Bind + 16, IWL_FW_CTXT_INVALID);
    IwlMvmPut32(Bind + 20, PhyId);
    if (IwlSendCmd(IWL_CMD_BINDING, Bind, sizeof(Bind), 1) != 0) {
        IwlMvmLogCmdFail("bind", IWL_CMD_BINDING);
        return 0;
    }
    IwlLogStage("bind=ok");
    return 1;
}

/*
 * 刀 #132：#131 证 auth 前 Prep → auth=to。
 * 刀 #139：#132/#138 证 assoc 前 Prep → 公司仅 Null、无 AssocResp。
 * Auth→Assoc 仍无 MAC；assoc=ok 后再 macadd+bind+TE。
 */
