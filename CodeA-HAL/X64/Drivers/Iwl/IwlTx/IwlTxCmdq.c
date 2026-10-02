/*
 * IwlTxCmdq.c — CMD 队列诊断 / 解楔 / FH 启停（PR-S-iwl-split-2）
 */
#include "IwlTxInternal.h"
#include "PhysicalMemory.h"
#include "HalSerial.h"

void IwlLogCmdq(UINT32 Seq) {
    char Line[64];
    char Hex[12];
    int n = 0;
    UINT32 Rd = 0;
    UINT32 Wr = 0;
    UINT32 St = 0;
    UINT32 Tssr = 0;
    UINT32 Terr = 0;
    const char *P = "cmdq ";

    if (IwlNicLock()) {
        Rd = IwlPrphR(IWL_SCD_QUEUE_RDPTR(IWL_CMD_QUEUE)) & IWL_TFD_Q_MASK;
        Wr = IwlPrphR(IWL_SCD_QUEUE_WRPTR(IWL_CMD_QUEUE)) & IWL_TFD_Q_MASK;
        St = IwlPrphR(IWL_SCD_QUEUE_STATUS_BITS(IWL_CMD_QUEUE));
        Tssr = IwlMmioR32(IWL_FH_TSSR_TX_STATUS);
        Terr = IwlMmioR32(IWL_FH_TSSR_TX_ERROR);
        IwlNicUnlock();
    }
    while (*P) {
        Line[n++] = *P++;
    }
    Line[n++] = 's';
    Line[n++] = 'w';
    Line[n++] = '=';
    HalSerialFormatHex(Hex, gCmdWrite & 0xffu, 2);
    Line[n++] = Hex[2];
    Line[n++] = Hex[3];
    Line[n++] = ' ';
    Line[n++] = 'h';
    Line[n++] = 'w';
    Line[n++] = '=';
    HalSerialFormatHex(Hex, Wr & 0xffu, 2);
    Line[n++] = Hex[2];
    Line[n++] = Hex[3];
    Line[n++] = ' ';
    Line[n++] = 'h';
    Line[n++] = 'r';
    Line[n++] = '=';
    HalSerialFormatHex(Hex, Rd & 0xffu, 2);
    Line[n++] = Hex[2];
    Line[n++] = Hex[3];
    Line[n++] = ' ';
    Line[n++] = 's';
    Line[n++] = '=';
    HalSerialFormatHex(Hex, Seq & 0xffu, 2);
    Line[n++] = Hex[2];
    Line[n++] = Hex[3];
    Line[n++] = ' ';
    Line[n++] = 'a';
    Line[n++] = '=';
    HalSerialFormatHex(Hex, (St >> IWL_SCD_STTS_ACTIVE_POS) & 1u, 2);
    Line[n++] = Hex[2];
    Line[n++] = Hex[3];
    Line[n] = 0;
    IwlLogVerb(Line);
    n = 0;
    P = "tssr=";
    while (*P) {
        Line[n++] = *P++;
    }
    HalSerialFormatHex(Hex, Tssr, 8);
    Line[n++] = Hex[2];
    Line[n++] = Hex[3];
    Line[n++] = Hex[4];
    Line[n++] = Hex[5];
    Line[n++] = Hex[6];
    Line[n++] = Hex[7];
    Line[n++] = Hex[8];
    Line[n++] = Hex[9];
    Line[n++] = ' ';
    Line[n++] = 'e';
    Line[n++] = '=';
    HalSerialFormatHex(Hex, Terr, 8);
    Line[n++] = Hex[2];
    Line[n++] = Hex[3];
    Line[n++] = Hex[4];
    Line[n++] = Hex[5];
    Line[n++] = Hex[6];
    Line[n++] = Hex[7];
    Line[n++] = Hex[8];
    Line[n++] = Hex[9];
    Line[n] = 0;
    IwlLogVerb(Line);
}

void IwlCmdqSnap(void) {
    IwlLogCmdq(0);
}

void IwlCmdKick(void) {
    if (!gTxReady) {
        return;
    }
    if (!IwlNicLock()) {
        return;
    }
    IwlMmioW32(IWL_HBUS_TARG_WRPTR,
               (IWL_CMD_QUEUE << 8) | (gCmdWrite & 0xff));
    IwlNicUnlock();
}

/*
 * 刀 #70：#69 证 scfg 的 0x0C 需下一条命令撞出，但该条 hr 卡死。
 * 轻量解楔：把 SCD RDPTR 推到 WRPTR（丢弃卡住 TFD），清 TSSR err，重 TXFACT。
 * 对照 #48 的 CmdqReset（更重且曾更糟）。
 */
int IwlCmdqUnwedge(void) {
    UINT32 Rd = 0;
    UINT32 Wr = 0;
    char Line[28];
    char Hex[12];
    int n = 0;
    const char *P;

    if (!gTxReady) {
        return 0;
    }
    if (!IwlNicLock()) {
        return 0;
    }
    Rd = IwlPrphR(IWL_SCD_QUEUE_RDPTR(IWL_CMD_QUEUE)) & IWL_TFD_Q_MASK;
    Wr = IwlPrphR(IWL_SCD_QUEUE_WRPTR(IWL_CMD_QUEUE)) & IWL_TFD_Q_MASK;
    if (Rd != Wr) {
        IwlPrphW(IWL_SCD_QUEUE_RDPTR(IWL_CMD_QUEUE), Wr);
    }
    IwlMmioW32(IWL_FH_TSSR_TX_ERROR, 0xffffffffu);
    IwlPrphW(IWL_SCD_TXFACT, 0xff);
    IwlMmioW32(IWL_HBUS_TARG_WRPTR,
               (IWL_CMD_QUEUE << 8) | (gCmdWrite & 0xff));
    IwlNicUnlock();
    gCmdRead = gCmdWrite & IWL_CMD_Q_MASK;

    P = (Rd != Wr) ? "uw=rd " : "uw=idle ";
    while (*P) {
        Line[n++] = *P++;
    }
    HalSerialFormatHex(Hex, Rd & 0xffu, 2);
    Line[n++] = Hex[2];
    Line[n++] = Hex[3];
    Line[n++] = '>';
    HalSerialFormatHex(Hex, Wr & 0xffu, 2);
    Line[n++] = Hex[2];
    Line[n++] = Hex[3];
    Line[n] = 0;
    IwlLogVerb(Line); /* #129：unwedge 常态噪音 */
    IwlCmdqSnap();
    return 1;
}

int IwlEnableAcTxq(UINT32 Qid, UINT32 Fifo) {
    UINT32 Stts;
    UINT32 Ctx2;

    if (gSchedBase == 0) {
        return 0;
    }
    IwlMmioW32(IWL_HBUS_TARG_WRPTR, (Qid << 8) | 0);
    IwlPrphW(IWL_SCD_QUEUE_STATUS_BITS(Qid),
             (0u << IWL_SCD_STTS_ACTIVE_POS)
             | (1u << IWL_SCD_STTS_ACT_EN_POS));
    IwlPrphW(IWL_SCD_AGGR_SEL, IwlPrphR(IWL_SCD_AGGR_SEL) & ~(1u << Qid));
    IwlPrphW(IWL_SCD_QUEUE_RDPTR(Qid), 0);
    IwlWriteMem32(gSchedBase + IWL_SCD_CONTEXT_QUEUE_OFF(Qid), 0);
    Ctx2 = ((IWL_FRAME_LIMIT << IWL_SCD_CTX_WIN_POS) & 0x7fu)
         | ((IWL_FRAME_LIMIT << IWL_SCD_CTX_FRAME_POS) & 0x7f0000u);
    IwlWriteMem32(gSchedBase + IWL_SCD_CONTEXT_QUEUE_OFF(Qid) + 4, Ctx2);
    Stts = (1u << IWL_SCD_STTS_ACTIVE_POS)
         | (Fifo << IWL_SCD_STTS_TXF_POS)
         | (1u << IWL_SCD_STTS_WSL_POS)
         | IWL_SCD_STTS_MSK;
    IwlPrphW(IWL_SCD_QUEUE_STATUS_BITS(Qid), Stts);
    /* 刀 #152：以前只有命令队列置 SCD_EN。q5 因此取不走 TFD，表现为 txa=none。 */
    IwlPrphW(IWL_SCD_EN_CTRL, IwlPrphR(IWL_SCD_EN_CTRL) | (1u << Qid));
    return 1;
}

int IwlEnableCmdTxq(void) {
    return IwlEnableAcTxq(IWL_CMD_QUEUE, IWL_TX_FIFO_CMD);
}

/* Linux iwl_pcie_tx_stop_fh：停通道并等到 IDLE */
int IwlFhTxDrain(void) {
    UINT32 Ch;
    UINT32 Try;
    UINT32 Mask = 0;
    UINT32 St;
    int Ok = 1;

    IwlPrphW(IWL_SCD_TXFACT, 0);
    for (Ch = 0; Ch < IWL_FH_TCSR_CHNL_NUM; Ch++) {
        IwlMmioW32(IWL_FH_TCSR_CHNL_CFG(Ch), 0);
        Mask |= (1u << Ch) << 16; /* TSSR no-pending / idle 位 */
    }
    for (Try = 0; Try < 250; Try++) {
        St = IwlMmioR32(IWL_FH_TSSR_TX_STATUS);
        /* IDLE = bits23:16 全 1（no pending）；buffers-empty 常不满，勿强求 */
        if (((St >> 16) & 0xffu) == 0xffu) {
            break;
        }
        IwlStallUs(20);
    }
    St = IwlMmioR32(IWL_FH_TSSR_TX_STATUS);
    if (((St >> 16) & 0xffu) != 0xffu) {
        Ok = 0;
    }
    IwlMmioW32(IWL_FH_TSSR_TX_ERROR, 0xffffffffu);
    (void)Mask;
    return Ok;
}

void IwlFhTxStart(void) {
    UINT32 Ch;

    for (Ch = 0; Ch < IWL_FH_TCSR_CHNL_NUM; Ch++) {
        IwlMmioW32(IWL_FH_TCSR_CHNL_CFG(Ch),
                   IWL_FH_TCSR_DMA_CHNL_EN | IWL_FH_TCSR_DMA_CREDIT_EN);
    }
    IwlMmioSet(IWL_FH_TX_CHICKEN, IWL_FH_TX_CHICKEN_RETRY);
    IwlPrphW(IWL_SCD_TXFACT, 0xff);
}

/*
 * 刀 #37：#36 证「重置 SCD 不够」——cqrst 后 hr=00、tssr=077F（ch7 pending）。
 * 先 FH stop+drain，再重置 cmdq。
 */
int IwlCmdqReset(void) {
    UINT32 Tssr;
    int DrainOk;
    char Line[40];
    char Hex[12];
    int n = 0;
    const char *P;

    if (!gTxReady || !gPostAliveOk) {
        return 0;
    }
    gCmdWrite = 0;
    gCmdRead = 0;
    if (gCmdTfd) {
        IwlTxZero(gCmdTfd, 8 * PAGE_SIZE);
        IwlFlushDma(gCmdTfd, 8 * PAGE_SIZE);
    }
    if (gBcTbl) {
        IwlTxZero(gBcTbl, 6 * PAGE_SIZE);
        IwlFlushDma(gBcTbl, 6 * PAGE_SIZE);
    }
    if (!IwlNicLock()) {
        return 0;
    }
    DrainOk = IwlFhTxDrain();
    Tssr = IwlMmioR32(IWL_FH_TSSR_TX_STATUS);
    if (!IwlEnableCmdTxq()) {
        IwlNicUnlock();
        IwlLogVerb("cqrst=cmdq");
        return 0;
    }
    IwlMmioW32(IWL_FH_KW_MEM_ADDR, (UINT32)(gKwPhys >> 4));
    IwlMmioW32(IWL_FH_CBBC_QUEUE(IWL_CMD_QUEUE), (UINT32)(gCmdTfdPhys >> 8));
    IwlFhTxStart();
    IwlNicUnlock();

    P = DrainOk ? "cqrst=ok t=" : "cqrst=drain t=";
    while (*P) {
        Line[n++] = *P++;
    }
    HalSerialFormatHex(Hex, Tssr, 8);
    Line[n++] = Hex[2];
    Line[n++] = Hex[3];
    Line[n++] = Hex[4];
    Line[n++] = Hex[5];
    Line[n++] = Hex[6];
    Line[n++] = Hex[7];
    Line[n++] = Hex[8];
    Line[n++] = Hex[9];
    Line[n] = 0;
    IwlLogVerb(Line); /* #129 */
    return 1;
}
