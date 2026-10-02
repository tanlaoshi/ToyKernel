/*
 * IwlTxFrame.c — 802.11 帧经 AUX/AP 队列发送（PR-S-iwl-split-2）
 */
#include "IwlTxInternal.h"
#include "PhysicalMemory.h"
#include "HalSerial.h"

int IwlSendFrameRaw(const UINT8 *Frame80211, UINTN Len) {
    UINT8 *Buf;
    IWL_TFD *Tfd;
    UINT32 Idx;
    UINT32 Next;
    UINT32 Slot;
    UINT32 Need;
    UINT32 Flags;
    UINT32 Rate;
    UINT32 Pad;
    UINT16 PmTo;
    UINT8 Subtype;
    UINT8 StaId;
    UINT32 Qid;
    UINTN i;
    UINT64 Phys;

    if (!Frame80211 || Len == 0 || Len > 400 || !gAuxReady || !gAuxBuf) {
        return -1;
    }
    /* QoS 头是 26 字节。固件要求在头后插入 2 字节填充，并用 MH_PAD 标明。 */
    Pad = (Frame80211[0] == 0x88u) ? 2u : 0u;
    Need = 4u + IWL_TX_CMD_HDR_SIZE + (UINT32)Len + Pad;
    if (Need > IWL_AUX_SLOT) {
        return -1;
    }
    /*
     * 刀 #162：q1 是辅助队列，管理帧能走，数据帧停住。
     * EAPOL 改走 q4 + 建站时就写好地址的 AP 站，FIFO 为 VO。
     */
    if (gApQReady && ((Frame80211[0] >> 2) & 0x3u) == 2u) {
        Qid = IWL_DQA_BSS_CLIENT_QUEUE;
        StaId = (UINT8)IWL_AP_STA_ID;
        if (!gEapAuxLogged) {
            gEapAuxLogged = 1;
            IwlLogStage("eapol=q4");
        }
    } else if (gApQReady) {
        Qid = IWL_DQA_BSS_CLIENT_QUEUE;
        StaId = (UINT8)IWL_AP_STA_ID;
    } else {
        Qid = IWL_DQA_AUX_QUEUE;
        StaId = (UINT8)IWL_AUX_STA_ID;
    }
    Idx = ((Qid == IWL_DQA_AUX_QUEUE) ? gAuxWrite : gApWrite) & IWL_TFD_Q_MASK;
    Slot = Idx % IWL_AUX_SLOTS;
    Buf = gAuxBuf + Slot * IWL_AUX_SLOT;
    Phys = gAuxBufPhys + (UINT64)Slot * IWL_AUX_SLOT;
    for (i = 0; i < IWL_AUX_SLOT; i++) {
        Buf[i] = 0;
    }
    Subtype = (UINT8)((Frame80211[0] >> 4) & 0x0fu);
    Flags = IWL_TX_CMD_FLG_ACK | IWL_TX_CMD_FLG_SEQ_CTL
          | IWL_TX_CMD_FLG_BT_DIS;
    /* EAPOL 继续 1M。已加密的数据帧用 6M：HT 接入点经常不确认 1M 数据。 */
    if ((Frame80211[1] & 0x40u) != 0) {
        Rate = IWL_RATE_6M_PLCP | IWL_RATE_MCS_ANT_A;
    } else if (Pad) {
        Flags |= IWL_TX_CMD_FLG_MH_PAD;
        Rate = IWL_RATE_6M_PLCP | IWL_RATE_MCS_ANT_A;
    } else {
        Rate = IWL_RATE_1M_PLCP | IWL_RATE_MCS_CCK | IWL_RATE_MCS_ANT_A;
    }
    PmTo = (Subtype == 0u || Subtype == 2u)
           ? (UINT16)IWL_PM_FRAME_ASSOC : (UINT16)IWL_PM_FRAME_MGMT;

    Buf[0] = (UINT8)IWL_CMD_TX;
    Buf[1] = 0;
    Buf[2] = (UINT8)Idx;
    Buf[3] = (UINT8)Qid;
    Buf[4 + 0] = (UINT8)(Len + Pad);
    Buf[4 + 1] = (UINT8)((Len + Pad) >> 8);
    Buf[4 + 4] = (UINT8)Flags;
    Buf[4 + 5] = (UINT8)(Flags >> 8);
    Buf[4 + 6] = (UINT8)(Flags >> 16);
    Buf[4 + 7] = (UINT8)(Flags >> 24);
    Buf[4 + 12] = (UINT8)Rate;
    Buf[4 + 13] = (UINT8)(Rate >> 8);
    Buf[4 + 14] = (UINT8)(Rate >> 16);
    Buf[4 + 15] = (UINT8)(Rate >> 24);
    Buf[4 + 16] = StaId;
    /* 已置 Protected 的数据帧：密钥放进 TX 命令，由固件加密并补 MIC。 */
    if ((Frame80211[1] & 0x40u) != 0) {
        Buf[4 + 17] = (UINT8)IWL_TX_CMD_SEC_CCM;
        for (i = 0; i < 16u; i++) {
            Buf[4 + 20 + i] = gIwlPtk[i];
        }
    }
    /*
     * 刀 #153：life/retry/tid 原先写在 reserved3（+36）。
     * dram_lsb_ptr（+44）因此变成非 0，数据帧按垃圾地址回写 scratch。
     * TX_CMD v6：life 在 +40，dram 保持 0，retry/tid/pm 从 +49 起。
     */
    Buf[4 + 40] = (UINT8)IWL_TX_CMD_LIFE_INFINITE;
    Buf[4 + 41] = (UINT8)(IWL_TX_CMD_LIFE_INFINITE >> 8);
    Buf[4 + 42] = (UINT8)(IWL_TX_CMD_LIFE_INFINITE >> 16);
    Buf[4 + 43] = (UINT8)(IWL_TX_CMD_LIFE_INFINITE >> 24);
    Buf[4 + 49] = 3;
    Buf[4 + 50] = 7;
    /* QoS 数据：tid 与帧头 QoS Control 一致。非 QoS（EAPOL）仍用 8。 */
    if (Frame80211[0] == 0x88u) {
        Buf[4 + 51] = (UINT8)(Frame80211[24] & 0x0Fu);
    } else {
        Buf[4 + 51] = (UINT8)IWL_MAX_TID_COUNT;
    }
    Buf[4 + 52] = (UINT8)PmTo;
    Buf[4 + 53] = (UINT8)(PmTo >> 8);
    if (Pad) {
        for (i = 0; i < 26u && i < Len; i++) {
            Buf[4 + IWL_TX_CMD_HDR_SIZE + i] = Frame80211[i];
        }
        for (i = 26u; i < Len; i++) {
            Buf[4 + IWL_TX_CMD_HDR_SIZE + Pad + i] = Frame80211[i];
        }
    } else {
        for (i = 0; i < Len; i++) {
            Buf[4 + IWL_TX_CMD_HDR_SIZE + i] = Frame80211[i];
        }
    }

    Tfd = (Qid == IWL_DQA_AUX_QUEUE) ? &gAuxTfd[Idx] : &gApTfd[Idx];
    IwlTxZero(Tfd, sizeof(*Tfd));
    IwlTfdSetTb(Tfd, 0, Phys, (UINT16)Need);
    IwlFlushDma(Buf, Need);
    IwlFlushDma(Tfd, sizeof(*Tfd));
    IwlUpdateSched(Qid, Idx, StaId, (UINT16)(Len + Pad));

    Next = (Idx + 1u) & IWL_TFD_Q_MASK;
    if (Qid == IWL_DQA_AUX_QUEUE) {
        gAuxWrite = Next;
    } else {
        gApWrite = Next;
    }
    if (!IwlNicLock()) {
        return -1;
    }
    IwlMmioW32(IWL_HBUS_TARG_WRPTR, (Qid << 8) | (Next & 0xffu));
    IwlNicUnlock();
    if (gAuxTxLog < 4u) {
        gAuxTxLog++;
        IwlLogVerb(Qid == IWL_DQA_AUX_QUEUE ? "tx=aux" : "tx=ap5");
    }
    return 0;
}

/* 刀 #45：认领错序暂存的 host 回包（按 opcode） */
int IwlRspStashClaim(UINT8 Code, UINT8 Idx) {
    if (!gRspStash) {
        return 0;
    }
    if (gRspStashCode == Code || (Idx != 0xffu && gRspStashIdx == Idx)) {
        gRspStash = 0;
        return 1;
    }
    return 0;
}
