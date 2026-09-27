/*
 * IwlMvm.c — ALIVE 后最小 MVM（刀 #23：DQA cmdq=0 + UMAC SCAN_CFG）
 *
 * phy(legacy) 在 q9 能通，但 LONG_GROUP/UMAC 只吃 DQA 命令队列 0。
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

static void IwlCopy(void *D, const void *S, UINTN N) {
    UINT8 *d = (UINT8 *)D;
    const UINT8 *s = (const UINT8 *)S;
    UINTN i;
    for (i = 0; i < N; i++) {
        d[i] = s[i];
    }
}

static void IwlLogCmdFail(const char *Tag, UINT32 Op) {
    char Line[40];
    char Hex[12];
    int n = 0;
    const char *P = Tag;
    while (*P && n < 24) {
        Line[n++] = *P++;
    }
    Line[n++] = ' ';
    Line[n++] = 'o';
    Line[n++] = '=';
    HalSerialFormatHex(Hex, Op, 2);
    Line[n++] = Hex[2];
    Line[n++] = Hex[3];
    Line[n] = 0;
    IwlLogStage(Line);
}

static int IwlMvmDqaEnable(void) {
    UINT8 Cmd[4];
    UINT32 Q = IWL_CMD_QUEUE; /* DQA cmd queue = 0 */

    IwlZero(Cmd, sizeof(Cmd));
    Cmd[0] = (UINT8)Q;
    Cmd[1] = (UINT8)(Q >> 8);
    Cmd[2] = (UINT8)(Q >> 16);
    Cmd[3] = (UINT8)(Q >> 24);
    if (IwlSendCmd(IWL_CMD_ID(IWL_DQA_ENABLE_CMD, IWL_DATA_PATH_GROUP, 0),
                   Cmd, sizeof(Cmd), 1) != 0) {
        IwlLogCmdFail("mvm=dqa", IWL_DQA_ENABLE_CMD);
        return 0;
    }
    IwlLogVerb("mvm=dqaok");
    return 1;
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

/*
 * 刀 #64：#63 证等 FH idle 无效（scfg=to，ACK 仍被 scan 撞出）。
 * OpenBSD 在 scfg 前有 phy_ctxt ADD；补最小 2.4G ch1 上下文。
 */
static int IwlMvmPhyCtxt(void) {
    UINT8 Cmd[36];
    UINT32 RxChain;
    UINT32 Ant = IWL_ANT_AB;
    UINT32 Nant = 2; /* A+B */

    IwlZero(Cmd, sizeof(Cmd));
    IwlPut32(Cmd + 0, 0);
    IwlPut32(Cmd + 4, IWL_FW_CTXT_ACTION_ADD);
    Cmd[16] = (UINT8)IWL_PHY_BAND_24;
    Cmd[17] = 1;
    Cmd[18] = (UINT8)IWL_PHY_VHT_CHANNEL_MODE20;
    Cmd[19] = (UINT8)IWL_PHY_VHT_CTRL_POS_1_BELOW;
    IwlPut32(Cmd + 20, Ant);
    RxChain = (Ant << IWL_PHY_RX_CHAIN_VALID_POS)
            | (Nant << IWL_PHY_RX_CHAIN_CNT_POS)
            | (Nant << IWL_PHY_RX_CHAIN_MIMO_CNT_POS);
    IwlPut32(Cmd + 24, RxChain);
    if (IwlSendCmd(IWL_CMD_PHY_CONTEXT, Cmd, sizeof(Cmd), 1) != 0) {
        IwlLogCmdFail("phyctx", IWL_CMD_PHY_CONTEXT);
        return 0;
    }
    IwlLogVerb("phyctx=ok");
    return 1;
}

/*
 * 刀 #66：#65 无 scfg 时 scan FH 通但 RX c=FF（拒绝）。
 * #61 PRPH-auxq+ADD_STA 毒队列；OpenBSD 是 SCD_QUEUE_CFG(0x1d) 后再 ADD_STA。
 */
static int IwlMvmAuxSta(void) {
    UINT8 Qcfg[IWL_SCD_TXQ_CFG_CMD_SIZE];
    UINT8 Sta[IWL_ADD_STA_CMD_SIZE];
    UINT32 Qid = IWL_DQA_AUX_QUEUE;
    UINT32 MacColor = IWL_FW_CMD_ID_AND_COLOR(IWL_MAC_INDEX_AUX, 0);

    if (IwlNicLock()) {
        IwlMmioW32(IWL_HBUS_TARG_WRPTR, (Qid << 8) | 0);
        IwlNicUnlock();
    }
    IwlZero(Qcfg, sizeof(Qcfg));
    Qcfg[0] = 0;
    Qcfg[1] = (UINT8)IWL_AUX_STA_ID;
    Qcfg[2] = (UINT8)IWL_MAX_TID_COUNT;
    Qcfg[3] = (UINT8)Qid;
    Qcfg[4] = 1;
    Qcfg[5] = 0;
    Qcfg[6] = (UINT8)IWL_TX_FIFO_MCAST;
    Qcfg[7] = (UINT8)IWL_FRAME_LIMIT;
    IwlPut16(Qcfg + 8, 0);
    if (IwlSendCmd(IWL_CMD_SCD_QUEUE_CFG, Qcfg, sizeof(Qcfg), 1) != 0) {
        IwlLogCmdFail("auxq", IWL_CMD_SCD_QUEUE_CFG);
        return 0;
    }
    IwlLogVerb("auxq=ok");

    IwlZero(Sta, sizeof(Sta));
    IwlPut16(Sta + 2, 0xffff);
    IwlPut32(Sta + 4, MacColor);
    Sta[16] = (UINT8)IWL_AUX_STA_ID;
    Sta[35] = (UINT8)IWL_STA_AUX_ACTIVITY;
    IwlPut32(Sta + 40, 1u << Qid);
    if (IwlSendCmd(IWL_CMD_ADD_STA, Sta, sizeof(Sta), 1) != 0) {
        IwlLogCmdFail("auxsta", IWL_CMD_ADD_STA);
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
    UINT32 Qid = IWL_DQA_MIN_MGMT_QUEUE;
    UINT32 Flg = IWL_STA_FLG_CLASS_AUTH | IWL_STA_FLG_CLASS_ASSOC;

    IwlZero(Qcfg, sizeof(Qcfg));
    Qcfg[0] = 0;
    Qcfg[1] = (UINT8)IWL_AP_STA_ID;
    Qcfg[2] = (UINT8)IWL_MAX_TID_COUNT;
    Qcfg[3] = (UINT8)Qid;
    Qcfg[4] = 1;
    Qcfg[5] = 0;
    Qcfg[6] = (UINT8)IWL_TX_FIFO_MCAST;
    Qcfg[7] = (UINT8)IWL_FRAME_LIMIT;
    IwlPut16(Qcfg + 8, 0);
    if (IwlSendCmd(IWL_CMD_SCD_QUEUE_CFG, Qcfg, sizeof(Qcfg), 150) != 0) {
        IwlLogCmdFail("apq", IWL_CMD_SCD_QUEUE_CFG);
        (void)IwlCmdqUnwedge();
        return 0;
    }

    IwlZero(Sta, sizeof(Sta));
    IwlPut16(Sta + 2, 0xffff);
    IwlPut32(Sta + 4, MacColor);
    IwlCopy(Sta + 8, gIwlBssid, 6);
    Sta[16] = (UINT8)IWL_AP_STA_ID;
    IwlPut32(Sta + 20, Flg);
    IwlPut32(Sta + 24, Flg);
    Sta[35] = (UINT8)IWL_STA_LINK;
    IwlPut16(Sta + 36, gIwlAid ? gIwlAid : 1u);
    IwlPut32(Sta + 40, 1u << Qid);
    if (IwlSendCmd(IWL_CMD_ADD_STA, Sta, sizeof(Sta), 200) != 0) {
        IwlLogCmdFail("apsta", IWL_CMD_ADD_STA);
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
    IwlZero(Cmd, sizeof(Cmd));
    IwlPut32(Cmd + 0, 0);
    IwlPut32(Cmd + 4, IWL_FW_CTXT_ACTION_MODIFY);
    Cmd[16] = (UINT8)IWL_PHY_BAND_24;
    Cmd[17] = Chan;
    Cmd[18] = (UINT8)IWL_PHY_VHT_CHANNEL_MODE20;
    Cmd[19] = (UINT8)IWL_PHY_VHT_CTRL_POS_1_BELOW;
    IwlPut32(Cmd + 20, Ant);
    RxChain = (Ant << IWL_PHY_RX_CHAIN_VALID_POS)
            | (Nant << IWL_PHY_RX_CHAIN_CNT_POS)
            | (Nant << IWL_PHY_RX_CHAIN_MIMO_CNT_POS);
    IwlPut32(Cmd + 24, RxChain);
    while (*P) {
        Line[n++] = *P++;
    }
    HalSerialFormatHex(Hex, Chan, 2);
    Line[n++] = Hex[2];
    Line[n++] = Hex[3];
    Line[n] = 0;
    if (IwlSendCmd(IWL_CMD_PHY_CONTEXT, Cmd, sizeof(Cmd), 1) != 0) {
        IwlLogCmdFail("phytune", IWL_CMD_PHY_CONTEXT);
        return 0;
    }
    IwlLogVerb(Line);
    return 1;
}

/*
 * 刀 #105/#106：BSS_STA。Prep 用 is_assoc=0；Assoc 后 MODIFY=1。
 */
static int IwlMacCtxtSend(UINT32 Action, int IsAssoc, int Sync) {
    UINT8 Cmd[IWL_MAC_CTX_CMD_SIZE];
    UINT32 MacId = IWL_FW_CMD_ID_AND_COLOR(0, 0);
    UINT32 Filter;
    UINTN i;
    UINT8 *Ac;

    IwlZero(Cmd, sizeof(Cmd));
    IwlPut32(Cmd + 0, MacId);
    IwlPut32(Cmd + 4, Action);
    IwlPut32(Cmd + 8, IWL_FW_MAC_TYPE_BSS_STA);
    IwlPut32(Cmd + 12, IWL_TSF_ID_A);
    IwlCopy(Cmd + 16, gIwlMac, 6);
    IwlCopy(Cmd + 24, gIwlBssid, 6);
    IwlPut32(Cmd + 32, 0x0fu);
    IwlPut32(Cmd + 36, 0xff0u);
    IwlPut32(Cmd + 44, IWL_MAC_FLG_SHORT_PREAMBLE);
    IwlPut32(Cmd + 48, IWL_MAC_FLG_SHORT_SLOT);
    Filter = IWL_MAC_FILTER_ACCEPT_GRP
           | IWL_MAC_FILTER_DIS_DECRYPT
           | IWL_MAC_FILTER_DIS_GRP_DECRYPT
           | IWL_MAC_FILTER_IN_CONTROL_AND_MGMT
           | IWL_MAC_FILTER_IN_PROMISC
           | IWL_MAC_FILTER_IN_BEACON; /* #120：assoc 后仍收 beacon；#119 去掉后 n=01 */
    IwlPut32(Cmd + 52, Filter);
    Ac = Cmd + 60;
    for (i = 0; i < 5; i++) {
        Ac[i * 8 + 0] = 0x0f;
        Ac[i * 8 + 2] = 0x3f;
        Ac[i * 8 + 4] = 1;
        Ac[i * 8 + 5] = (UINT8)(1u << (i < 4 ? i : 0));
    }
    if (IsAssoc) {
        /* Linux iwl_mac_data_sta @100：勿写错 bi/AID 偏移（#120 写到 dtim_tsf 致 cmdto） */
        IwlPut32(Cmd + 100, 1);                         /* is_assoc */
        IwlPut32(Cmd + 104, 0);                         /* dtim_time */
        /* 108..115 dtim_tsf = 0 */
        IwlPut32(Cmd + 116, 100);                       /* bi TU */
        IwlPut32(Cmd + 120, 0);                         /* reserved1 */
        IwlPut32(Cmd + 124, 100);                       /* dtim_interval */
        IwlPut32(Cmd + 128, 0);                         /* data_policy */
        IwlPut32(Cmd + 132, 10);                        /* listen_interval */
        IwlPut32(Cmd + 136, gIwlAid ? (UINT32)gIwlAid : 1u); /* assoc_id */
        IwlPut32(Cmd + 140, 0);                         /* assoc_beacon_arrive_time */
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

    IwlZero(Cmd, sizeof(Cmd));
    IwlPut32(Cmd + 0, MacId);
    IwlPut32(Cmd + 4, IWL_FW_CTXT_ACTION_ADD);
    IwlPut32(Cmd + 8, IWL_TE_BSS_STA_AGGRESSIVE_ASSOC);
    IwlPut32(Cmd + 16, 500);   /* max_delay */
    IwlPut32(Cmd + 24, 1);
    IwlPut32(Cmd + 28, 12000); /* ~12s */
    Cmd[32] = 1;
    Cmd[33] = (UINT8)IWL_TE_V2_FRAG_NONE;
    Policy = (UINT16)(IWL_TE_V2_NOTIF_HOST_EVENT_START
                     | IWL_TE_V2_NOTIF_HOST_EVENT_END
                     | IWL_TE_V2_START_IMMEDIATELY);
    IwlPut16(Cmd + 34, Policy);
    if (IwlSendCmd(IWL_CMD_TIME_EVENT, Cmd, sizeof(Cmd), 200) != 0) {
        IwlLogCmdFail("te", IWL_CMD_TIME_EVENT);
        (void)IwlCmdqUnwedge();
        return;
    }
    IwlLogStage("te=ok");
}

static int IwlBindingAdd(void) {
    UINT8 Bind[IWL_BINDING_CMD_V1_SIZE];
    UINT32 MacId = IWL_FW_CMD_ID_AND_COLOR(0, 0);
    UINT32 PhyId = IWL_FW_CMD_ID_AND_COLOR(0, 0);

    IwlZero(Bind, sizeof(Bind));
    IwlPut32(Bind + 0, PhyId);
    IwlPut32(Bind + 4, IWL_FW_CTXT_ACTION_ADD);
    IwlPut32(Bind + 8, MacId);
    IwlPut32(Bind + 12, IWL_FW_CTXT_INVALID);
    IwlPut32(Bind + 16, IWL_FW_CTXT_INVALID);
    IwlPut32(Bind + 20, PhyId);
    if (IwlSendCmd(IWL_CMD_BINDING, Bind, sizeof(Bind), 1) != 0) {
        IwlLogCmdFail("bind", IWL_CMD_BINDING);
        return 0;
    }
    IwlLogStage("bind=ok");
    return 1;
}

/*
 * 刀 #132：#131 证 auth 前 Prep → auth=to。
 * Auth 成功后再 macadd+bind+TE，盖住随后 Assoc→M1 窗。
 */
int IwlMacCtxtPrep(void) {
    UINTN i;

    for (i = 0; i < 6; i++) {
        gIwlBssid[i] = gIwlTarget.Bssid[i];
    }
    if (!IwlMacCtxtSend(IWL_FW_CTXT_ACTION_ADD, 0, 1)) {
        IwlLogCmdFail("macadd", IWL_CMD_MAC_CONTEXT);
        (void)IwlCmdqUnwedge();
        return 0;
    }
    IwlLogStage("macadd=bss0");
    if (!IwlBindingAdd()) {
        return 0;
    }
    IwlProtectSession();
    return 1;
}

/*
 * 刀 #123/#125/#128：apsta → macmod → apqhw。
 * 刀 #132：mac/bind/TE 已在 auth 后 Prep。
 */
int IwlMacCtxtAssoc(void) {
    int ApOk;

    ApOk = IwlAddApSta();
    if (!ApOk) {
        IwlLogStage("apsta=soft");
    }
    if (IwlMacCtxtSend(IWL_FW_CTXT_ACTION_MODIFY, 1, 200)) {
        IwlLogStage("macmod=ok");
    } else {
        IwlLogStage("macmod=soft");
        (void)IwlCmdqUnwedge();
    }
    if (ApOk) {
        if (!IwlEnableApTxq()) {
            IwlLogStage("apqhw=soft");
            IwlAuxTxLogReset();
        }
    }
    return 1;
}

static int IwlMvmScanCfg(void) {
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
    IwlZero(Buf, sizeof(Buf));
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
    IwlPut32(Buf + 0, Flags);
    IwlPut32(Buf + 4, IWL_ANT_AB);
    IwlPut32(Buf + 8, IWL_ANT_AB);
    Rates |= IWL_SCAN_CFG_SUPPORTED_RATE(Rates);
    IwlPut32(Buf + 12, Rates);
    Buf[24] = 10;
    Buf[25] = 110;
    Buf[26] = 44;
    Buf[27] = 90;
    IwlCopy(Buf + 28, gIwlMac, 6);
    Buf[34] = (UINT8)IWL_AUX_STA_ID;
    Buf[35] = 0;
    for (i = 0; i < Nchan; i++) {
        Buf[36 + i] = Chans[i];
    }
    if (IwlSendCmd(IWL_CMD_ID(IWL_CMD_SCAN_CFG, IWL_LONG_GROUP, 0),
                   Buf, (UINT32)sizeof(Buf), -1) != 0) {
        IwlLogCmdFail("mvm=scfg", IWL_CMD_SCAN_CFG);
        return 0;
    }
    IwlZero(Kick, sizeof(Kick));
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

    IwlZero(Kick, sizeof(Kick));
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
static int IwlMvmMcc(void) {
    UINT8 Mcc[IWL_MCC_UPDATE_SIZE];

    IwlZero(Mcc, sizeof(Mcc));
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
static int IwlMvmAntCanary(void) {
    UINT8 Cmd[4];
    UINT32 Ant = IWL_ANT_AB;

    IwlZero(Cmd, sizeof(Cmd));
    Cmd[0] = (UINT8)Ant;
    Cmd[1] = (UINT8)(Ant >> 8);
    Cmd[2] = (UINT8)(Ant >> 16);
    Cmd[3] = (UINT8)(Ant >> 24);
    if (IwlSendCmd(IWL_CMD_TX_ANT_CFG, Cmd, sizeof(Cmd), 1) != 0) {
        IwlLogCmdFail("ant1", IWL_CMD_TX_ANT_CFG);
        return 0;
    }
    IwlLogVerb("ant1=ok");
    if (IwlSendCmd(IWL_CMD_TX_ANT_CFG, Cmd, sizeof(Cmd), 1) != 0) {
        IwlLogCmdFail("ant2", IWL_CMD_TX_ANT_CFG);
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
