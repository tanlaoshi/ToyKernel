/*
 * IwlTxAux.c — AUX/AP 队列与 post_alive SCD（PR-S-iwl-split-2）
 */
#include "IwlTxInternal.h"
#include "PhysicalMemory.h"
#include "HalSerial.h"

int IwlEnableAuxTxq(void) {
    /*
     * 刀 #83：#82 cmderr o=1C — TX_CMD 不能走 cmdq。绑 AUX CBBC + SCD。
     */
    if (!gTxReady || !gAuxTfd) {
        return 0;
    }
    if (!IwlNicLock()) {
        return 0;
    }
    IwlMmioW32(IWL_FH_CBBC_QUEUE(IWL_DQA_AUX_QUEUE),
               (UINT32)(gAuxTfdPhys >> 8));
    IwlFlushDma(gAuxTfd, 8 * PAGE_SIZE);
    if (!IwlEnableAcTxq(IWL_DQA_AUX_QUEUE, IWL_TX_FIFO_MCAST)) {
        IwlNicUnlock();
        return 0;
    }
    IwlNicUnlock();
    gAuxWrite = 0;
    gAuxReady = 1;
    IwlLogVerb("auxhw=ok");
    return 1;
}

void IwlAuxTxLogReset(void) {
    gAuxTxLog = 0;
}

/*
 * 刀 #155：#154 为 q5r=00 w=05。SCD_QUEUE_CFG 之后再写 context=0，
 * 把固件刚设的额度清掉，调度器不取描述符。环基址改在该命令之前绑上。
 */
int IwlPrepareApTxq(void) {
    UINT32 Qid = IWL_DQA_BSS_CLIENT_QUEUE;

    if (!gTxReady || !gApTfd) {
        return 0;
    }
    if (!IwlNicLock()) {
        return 0;
    }
    IwlMmioW32(IWL_FH_CBBC_QUEUE(Qid), (UINT32)(gApTfdPhys >> 8));
    IwlFlushDma(gApTfd, 8 * PAGE_SIZE);
    IwlMmioW32(IWL_HBUS_TARG_WRPTR, (Qid << 8) | 0);
    IwlNicUnlock();
    gApWrite = 0;
    return 1;
}

int IwlEnableApTxq(void) {
    UINT32 Qid = IWL_DQA_BSS_CLIENT_QUEUE;

    if (!gTxReady || !gApTfd || !gAuxReady || gSchedBase == 0) {
        return 0;
    }
    if (!IwlNicLock()) {
        return 0;
    }
    /*
     * 刀 #162：q1 能取管理帧，是因为 SCD_QUEUE_CFG 之后走了完整 EnableAcTxq。
     * q5 只或了几个状态位，固件又写回 0x9D。数据改走 q4，按 q1 的方式打开。
     */
    IwlMmioW32(IWL_FH_CBBC_QUEUE(Qid), (UINT32)(gApTfdPhys >> 8));
    if (!IwlEnableAcTxq(Qid, IWL_TX_FIFO_VO)) {
        IwlNicUnlock();
        return 0;
    }
    IwlPrphW(IWL_SCD_QUEUECHAIN_SEL,
             IwlPrphR(IWL_SCD_QUEUECHAIN_SEL) | (1u << Qid));
    IwlNicUnlock();
    gApWrite = 0;
    gApQReady = 1;
    IwlAuxTxLogReset();
    IwlLogStage("apqhw=ok");
    return 1;
}

void IwlLogApQ(void) {
    char Line[48];
    char Hex[12];
    int n = 0;
    const char *P = "q4r=";
    UINT32 Rd = 0;
    UINT32 Wr = 0;
    UINT32 St = 0;
    UINT32 Ch = 0;

    if (IwlNicLock()) {
        Rd = IwlPrphR(IWL_SCD_QUEUE_RDPTR(IWL_DQA_BSS_CLIENT_QUEUE)) & 0xffu;
        Wr = IwlPrphR(IWL_SCD_QUEUE_WRPTR(IWL_DQA_BSS_CLIENT_QUEUE)) & 0xffu;
        St = IwlPrphR(IWL_SCD_QUEUE_STATUS_BITS(IWL_DQA_BSS_CLIENT_QUEUE));
        Ch = IwlPrphR(IWL_SCD_QUEUECHAIN_SEL);
        IwlNicUnlock();
    }
    while (*P) {
        Line[n++] = *P++;
    }
    HalSerialFormatHex(Hex, Rd, 2);
    Line[n++] = Hex[2];
    Line[n++] = Hex[3];
    Line[n++] = ' ';
    Line[n++] = 'w';
    Line[n++] = '=';
    HalSerialFormatHex(Hex, Wr, 2);
    Line[n++] = Hex[2];
    Line[n++] = Hex[3];
    Line[n++] = ' ';
    Line[n++] = 'a';
    Line[n++] = '=';
    HalSerialFormatHex(Hex, St, 8);
    Line[n++] = Hex[2];
    Line[n++] = Hex[3];
    Line[n++] = Hex[4];
    Line[n++] = Hex[5];
    Line[n++] = Hex[6];
    Line[n++] = Hex[7];
    Line[n++] = Hex[8];
    Line[n++] = Hex[9];
    Line[n++] = ' ';
    Line[n++] = 'c';
    Line[n++] = '=';
    HalSerialFormatHex(Hex, Ch, 8);
    Line[n++] = Hex[2];
    Line[n++] = Hex[3];
    Line[n++] = Hex[4];
    Line[n++] = Hex[5];
    Line[n++] = Hex[6];
    Line[n++] = Hex[7];
    Line[n++] = Hex[8];
    Line[n++] = Hex[9];
    Line[n] = 0;
    IwlLogStage(Line);
    n = 0;
    P = "q1r=";
    Rd = 0;
    Wr = 0;
    if (IwlNicLock()) {
        Rd = IwlPrphR(IWL_SCD_QUEUE_RDPTR(IWL_DQA_AUX_QUEUE)) & 0xffu;
        Wr = IwlPrphR(IWL_SCD_QUEUE_WRPTR(IWL_DQA_AUX_QUEUE)) & 0xffu;
        IwlNicUnlock();
    }
    while (*P) {
        Line[n++] = *P++;
    }
    HalSerialFormatHex(Hex, Rd, 2);
    Line[n++] = Hex[2];
    Line[n++] = Hex[3];
    Line[n++] = ' ';
    Line[n++] = 'w';
    Line[n++] = '=';
    HalSerialFormatHex(Hex, Wr, 2);
    Line[n++] = Hex[2];
    Line[n++] = Hex[3];
    Line[n] = 0;
    IwlLogStage(Line);
}

int IwlPostAlive(void) {
    UINT32 Words;
    UINT32 i;
    UINT32 PrphBase = 0;

    gPostAliveOk = 0;
    if (!gTxReady || !IwlNicLock()) {
        IwlLogStage("scd=lock");
        return 0;
    }
    PrphBase = IwlPrphR(IWL_SCD_SRAM_BASE_ADDR);
    if (gIwlSchedBase == 0) {
        gIwlSchedBase = PrphBase;
    }
    gSchedBase = gIwlSchedBase ? gIwlSchedBase : PrphBase;
    if (gSchedBase == 0) {
        IwlNicUnlock();
        IwlLogStage("scd=base0");
        return 0;
    }

    /* 清 SCD context→trans 表 */
    Words = (IWL_SCD_TRANS_TBL_MEM_HI - IWL_SCD_CONTEXT_MEM_LO) / 4u;
    for (i = 0; i < Words; i++) {
        IwlWriteMem32(gSchedBase + IWL_SCD_CONTEXT_MEM_LO + i * 4u, 0);
    }

    IwlPrphW(IWL_SCD_DRAM_BASE_ADDR, (UINT32)(gBcPhys >> 10));
    IwlPrphW(IWL_SCD_CHAINEXT_EN, 0);
    /* 刀 #34：RT 上片后再绑 CBBC/KW，防 FH 服务通道冲掉 */
    IwlMmioW32(IWL_FH_KW_MEM_ADDR, (UINT32)(gKwPhys >> 4));
    IwlMmioW32(IWL_FH_CBBC_QUEUE(IWL_CMD_QUEUE), (UINT32)(gCmdTfdPhys >> 8));
    IwlFlushDma(gCmdTfd, 8 * PAGE_SIZE);
    IwlFlushDma(gBcTbl, 6 * PAGE_SIZE);
    if (!IwlEnableCmdTxq()) {
        IwlNicUnlock();
        IwlLogStage("scd=cmdq");
        return 0;
    }
    IwlFhTxStart();
    IwlNicUnlock();
    gPostAliveOk = 1;
    IwlLogVerb("scd=ok");
    return 1;
}
