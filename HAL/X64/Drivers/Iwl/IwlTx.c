/*
 * IwlTx.c — CMD 队列 + post_alive SCD + host command（PR-N-wifi-2）
 *
 * TFD 必须是 HW 128B 布局（reserved[3]+num_tbs+tbs[20]）。
 * RT ALIVE 后须 SCD_TXFACT=0xff、使能 cmd 队列、开 FH DMA 通道。
 */
#include "IwlPrivate.h"
#include "PhysicalMemory.h"
#include "HalSerial.h"

static IWL_TFD *gCmdTfd;
static UINT64 gCmdTfdPhys;
static UINT8 *gCmdBufs[IWL_CMD_Q_SIZE];
static UINT64 gCmdBufPhys[IWL_CMD_Q_SIZE];
static IWL_TFD *gAuxTfd;
static UINT64 gAuxTfdPhys;
/* 刀 #84：多槽 AUX 缓冲，避免等 RDPTR 时在 LwIp CLI 锁里 Stall→键鼠假死 */
#define IWL_AUX_SLOTS  16u
#define IWL_AUX_SLOT   512u
static UINT8 *gAuxBuf;
static UINT64 gAuxBufPhys;
static UINT32 gAuxWrite;
static UINT32 gAuxTxLog;
static int gApQReady; /* #127：q5 HW 已绑，SendFrame 走 AP_STA */
static UINT8 *gFirstTbBase; /* 32 × 64B：Linux FIRST_TB 双向 DMA 窗 */
static UINT64 gFirstTbPhys;
static UINT8 *gKwPage;
static UINT64 gKwPhys;
static UINT16 *gBcTbl; /* [qid][idx] 展平：qid * IWL_TFD_BC_SIZE + idx */
static UINT64 gBcPhys;
static UINT32 gSchedBase;
static UINT32 gCmdWrite;
static UINT32 gCmdRead;
static int gTxReady;
static int gPostAliveOk;
static int gAuxReady;
/* 刀 #45：错序 host 回包暂存（scfg 被下一命令撞出时不被 rxmiss 丢掉） */
static int gRspStash;
static UINT8 gRspStashCode;
static UINT8 gRspStashIdx;
static UINT8 gRspStashQid;

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

static void IwlWriteMem32(UINT32 Addr, UINT32 Val) {
    IwlMmioW32(IWL_HBUS_TARG_MEM_WADDR, Addr);
    IwlMmioW32(IWL_HBUS_TARG_MEM_WDAT, Val);
}

static void IwlUpdateSched(UINT32 Qid, UINT32 Idx, UINT8 StaId, UINT16 Len) {
    UINT16 Val;
    UINT32 Off;
    UINT32 Dup;

    if (!gBcTbl) {
        return;
    }
    Len = (UINT16)(Len + IWL_TX_CRC_SIZE + IWL_TX_DELIMITER_SIZE);
    /* 8000 常用 DW_BC_TABLE：以 DWORD 计 */
    Len = (UINT16)((Len + 3u) / 4u);
    Val = (UINT16)(((UINT32)StaId << 12) | (Len & 0xfffu));
    Off = Qid * IWL_TFD_BC_SIZE + Idx;
    gBcTbl[Off] = Val;
    IwlFlushDma(&gBcTbl[Off], sizeof(UINT16));
    if (Idx < 64) {
        Dup = Qid * IWL_TFD_BC_SIZE + IWL_RX_Q_SIZE + Idx;
        gBcTbl[Dup] = Val;
        IwlFlushDma(&gBcTbl[Dup], sizeof(UINT16));
    }
}

static void IwlTfdSetTb(IWL_TFD *Tfd, UINT8 Idx, UINT64 Phys, UINT16 Len) {
    Tfd->Tb[Idx].Lo = (UINT32)Phys;
    Tfd->Tb[Idx].HiNLen = (UINT16)(((Phys >> 32) & 0xFu)
                                   | ((Len & 0xfffu) << 4));
    Tfd->NumTbs = (UINT8)(Idx + 1u);
}

static void IwlLogCmdq(UINT32 Seq) {
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

static int IwlEnableAcTxq(UINT32 Qid, UINT32 Fifo) {
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
    if (Qid == IWL_CMD_QUEUE) {
        IwlPrphW(IWL_SCD_EN_CTRL, IwlPrphR(IWL_SCD_EN_CTRL) | (1u << Qid));
    }
    return 1;
}

static int IwlEnableCmdTxq(void) {
    return IwlEnableAcTxq(IWL_CMD_QUEUE, IWL_TX_FIFO_CMD);
}

/* Linux iwl_pcie_tx_stop_fh：停通道并等到 IDLE */
static int IwlFhTxDrain(void) {
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

static void IwlFhTxStart(void) {
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
        IwlZero(gCmdTfd, 8 * PAGE_SIZE);
        IwlFlushDma(gCmdTfd, 8 * PAGE_SIZE);
    }
    if (gBcTbl) {
        IwlZero(gBcTbl, 6 * PAGE_SIZE);
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
 * 刀 #127：#126 证 AUX+AUX_STA 发 EAPOL-Start 仍 u=00。
 * ADD_STA 已挂 q5，但从未 CBBC/EnableAcTxq — 数据帧到不了 AP。
 * 复用 AUX TFD/槽（assoc 已完，不再并发表）；FIFO=VO。
 */
int IwlEnableApTxq(void) {
    UINT32 Qid = IWL_DQA_MIN_MGMT_QUEUE;

    if (!gTxReady || !gAuxTfd || !gAuxReady) {
        return 0;
    }
    if (!IwlNicLock()) {
        return 0;
    }
    IwlMmioW32(IWL_FH_CBBC_QUEUE(Qid), (UINT32)(gAuxTfdPhys >> 8));
    IwlFlushDma(gAuxTfd, 8 * PAGE_SIZE);
    if (!IwlEnableAcTxq(Qid, IWL_TX_FIFO_VO)) {
        IwlNicUnlock();
        return 0;
    }
    IwlNicUnlock();
    gAuxWrite = 0;
    gApQReady = 1;
    IwlAuxTxLogReset();
    IwlLogStage("apqhw=ok");
    return 1;
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

int IwlTxInit(void) {
    void *Mem;
    UINT32 i;
    UINT32 BcPages;

    if (!gTxReady) {
        Mem = PhysicalMemoryAllocatePages(1);
        if (!Mem) {
            return 0;
        }
        IwlZero(Mem, PAGE_SIZE);
        gKwPage = (UINT8 *)Mem;
        gKwPhys = (UINT64)(UINTN)Mem;

        /* 刀 #33：256 × 128B TFD = 8 页（HW 环长，非 32） */
        Mem = PhysicalMemoryAllocatePages(8);
        if (!Mem) {
            return 0;
        }
        IwlZero(Mem, 8 * PAGE_SIZE);
        gCmdTfd = (IWL_TFD *)Mem;
        gCmdTfdPhys = (UINT64)(UINTN)Mem;

        for (i = 0; i < IWL_CMD_Q_SIZE; i++) {
            Mem = PhysicalMemoryAllocatePages(1);
            if (!Mem) {
                return 0;
            }
            IwlZero(Mem, PAGE_SIZE);
            gCmdBufs[i] = (UINT8 *)Mem;
            gCmdBufPhys[i] = (UINT64)(UINTN)Mem;
        }

        /* FIRST_TB：32 × 64B 装一页（Linux bidirectional TB0） */
        Mem = PhysicalMemoryAllocatePages(1);
        if (!Mem) {
            return 0;
        }
        IwlZero(Mem, PAGE_SIZE);
        gFirstTbBase = (UINT8 *)Mem;
        gFirstTbPhys = (UINT64)(UINTN)Mem;

        /* 刀 #83/#84：AUX TFD 环 + 16×512B 槽（数据面不 Stall） */
        Mem = PhysicalMemoryAllocatePages(8);
        if (!Mem) {
            return 0;
        }
        IwlZero(Mem, 8 * PAGE_SIZE);
        gAuxTfd = (IWL_TFD *)Mem;
        gAuxTfdPhys = (UINT64)(UINTN)Mem;
        Mem = PhysicalMemoryAllocatePages(2);
        if (!Mem) {
            return 0;
        }
        IwlZero(Mem, 2 * PAGE_SIZE);
        gAuxBuf = (UINT8 *)Mem;
        gAuxBufPhys = (UINT64)(UINTN)Mem;
        gAuxTxLog = 0;

        /* BC 表：31 queues × 320 × u16 ≈ 20KB → 6 页（1KB 对齐） */
        BcPages = 6;
        Mem = PhysicalMemoryAllocatePages(BcPages);
        if (!Mem) {
            return 0;
        }
        IwlZero(Mem, BcPages * PAGE_SIZE);
        gBcTbl = (UINT16 *)Mem;
        gBcPhys = (UINT64)(UINTN)Mem;
        gTxReady = 1;
    }
    gCmdWrite = 0;
    gCmdRead = 0;
    gAuxWrite = 0;
    gPostAliveOk = 0;
    gAuxReady = 0;
    gApQReady = 0;

    if (!IwlNicLock()) {
        return 0;
    }
    IwlPrphW(IWL_SCD_TXFACT, 0);
    IwlMmioW32(IWL_FH_KW_MEM_ADDR, (UINT32)(gKwPhys >> 4));
    IwlMmioW32(IWL_FH_CBBC_QUEUE(IWL_CMD_QUEUE), (UINT32)(gCmdTfdPhys >> 8));
    IwlMmioSet(IWL_FH_TX_CHICKEN, IWL_FH_TX_CHICKEN_RETRY);
    IwlPrphW(IWL_SCD_GP_CTRL, IWL_SCD_GP_AUTO_ACTIVE | IWL_SCD_GP_ENABLE_31_QUEUES);
    IwlNicUnlock();
    IwlMmioSet(IWL_CSR_MAC_SHADOW_CTRL, 0x800fffff);
    IwlFlushDma(gKwPage, PAGE_SIZE);
    IwlFlushDma(gCmdTfd, 8 * PAGE_SIZE);
    IwlFlushDma(gBcTbl, 6 * PAGE_SIZE);
    return 1;
}

int IwlNicInit(void) {
    if (!IwlRxInit()) {
        return 0;
    }
    if (!IwlTxInit()) {
        return 0;
    }
    return 1;
}

int IwlSendCmd(UINT32 Id, const void *Data, UINT32 Len, int Sync) {
    UINT32 Slot;
    UINT32 Seq;
    UINT8 *Buf;
    IWL_TFD *Tfd;
    UINT32 Need;
    UINT32 Wait;
    UINT32 Group = (Id >> 8) & 0xffu;
    UINT32 Opcode = Id & 0xffu;
    UINT32 Version = (Id >> 16) & 0xffu;
    UINT32 HdrLen = Group ? sizeof(IWL_CMD_HDR_WIDE) : sizeof(IWL_CMD_HDR);
    UINT8 LastCode = 0;
    UINT32 RxHits = 0;

    if (!gTxReady || Len > IWL_CMD_PAYLOAD_MAX || HdrLen + Len > PAGE_SIZE) {
        return -1;
    }
    /*
     * Linux：sequence 用 write_ptr 低 8 位（环 256）；cmd 缓冲窗口 32。
     * 以前用 Slot 当 sequence，宽命令回包对不上会误判超时。
     */
    Seq = gCmdWrite & 0xffu;
    Slot = Seq & IWL_CMD_Q_MASK;
    Buf = gCmdBufs[Slot];
    IwlZero(Buf, HdrLen + Len);
    if (Group) {
        IWL_CMD_HDR_WIDE *W = (IWL_CMD_HDR_WIDE *)Buf;
        W->Opcode = (UINT8)Opcode;
        W->GroupId = (UINT8)Group;
        W->Idx = (UINT8)Seq;
        W->Qid = (UINT8)IWL_CMD_QUEUE;
        W->Length = (UINT16)Len;
        W->Reserved = 0;
        W->Version = (UINT8)Version;
    } else {
        IWL_CMD_HDR *H = (IWL_CMD_HDR *)Buf;
        H->Code = (UINT8)Opcode;
        H->Flags = 0;
        H->Idx = (UINT8)Seq;
        H->Qid = (UINT8)IWL_CMD_QUEUE;
    }
    if (Data && Len) {
        IwlCopy(Buf + HdrLen, Data, Len);
    }
    Need = HdrLen + Len;
    /*
     * 刀 #78：#77 证 SCAN_REQ DMA 完但 cmdto o=0D。刀 #37 注释写
     * 「BC 用真实 Need」，调用却仍传 0→BC=2（仅 CRC+DELIM）。OpenBSD
     * 对 cmd 也传 0，但我们大包（L0634）不 ACK；小包碰巧能过。改回 Need。
     */
    {
        UINT64 Phys = gCmdBufPhys[Slot];
        Tfd = &gCmdTfd[Seq & IWL_TFD_Q_MASK];
        IwlZero(Tfd, sizeof(*Tfd));
        IwlTfdSetTb(Tfd, 0, Phys, (UINT16)Need);
        IwlFlushDma(Buf, Need);
        IwlFlushDma(Tfd, sizeof(*Tfd));
    }
    IwlUpdateSched(IWL_CMD_QUEUE, Seq & IWL_TFD_Q_MASK, 0, (UINT16)Need);

    gCmdWrite = (gCmdWrite + 1) & IWL_TFD_Q_MASK;
    /* Sync < 0：只入队不响铃（刀 #56 与下一命令合并 WRPTR） */
    if (Sync >= 0) {
        if (!IwlNicLock()) {
            return -1;
        }
        IwlMmioW32(IWL_HBUS_TARG_WRPTR,
                   (IWL_CMD_QUEUE << 8) | (gCmdWrite & 0xff));
        IwlNicUnlock();
    }

    if (Sync <= 0) {
        return 0;
    }
    /*
     * Sync==1：兼容旧 2.5s。
     * Sync>=2：等 Sync ms（刀 #100：apsta 短等，勿楔死 msg1 窗）。
     */
    {
        UINT32 WaitMax = (Sync == 1) ? 2500u : (UINT32)Sync;
        if (WaitMax > 5000u) {
            WaitMax = 5000u;
        }
        for (Wait = 0; Wait < WaitMax; Wait++) {
        IWL_RX_PKT *Pkt;
        UINTN RLen;

        IwlRxPoll();
        while (IwlRxTake(&Pkt, &RLen)) {
            UINT8 Ridx = Pkt->Hdr.Idx;
            UINT8 Rqid = Pkt->Hdr.Qid;
            UINT8 Code = Pkt->Hdr.Code;
            const UINT8 *Pay;
            LastCode = Code;
            RxHits++;
            if (Code == 0x02u && RLen >= sizeof(IWL_CMD_HDR) + 4 + 8) {
                UINT16 BadSeq;
                Pay = Pkt->Data;
                BadSeq = (UINT16)Pay[6] | ((UINT16)Pay[7] << 8);
                if (Ridx == (UINT8)Seq || BadSeq == (UINT16)Seq) {
                    char Line[56];
                    char Hex[12];
                    int n = 0;
                    const char *P = "cmderr e=";
                    UINT32 Et = (UINT32)Pay[0] | ((UINT32)Pay[1] << 8)
                              | ((UINT32)Pay[2] << 16) | ((UINT32)Pay[3] << 24);
                    while (*P) {
                        Line[n++] = *P++;
                    }
                    HalSerialFormatHex(Hex, Et, 8);
                    Line[n++] = Hex[2];
                    Line[n++] = Hex[3];
                    Line[n++] = Hex[4];
                    Line[n++] = Hex[5];
                    Line[n++] = Hex[6];
                    Line[n++] = Hex[7];
                    Line[n++] = Hex[8];
                    Line[n++] = Hex[9];
                    Line[n++] = ' ';
                    Line[n++] = 'o';
                    Line[n++] = '=';
                    HalSerialFormatHex(Hex, Pay[4], 2);
                    Line[n++] = Hex[2];
                    Line[n++] = Hex[3];
                    Line[n++] = ' ';
                    Line[n++] = 'w';
                    Line[n++] = '=';
                    HalSerialFormatHex(Hex, Opcode, 2);
                    Line[n++] = Hex[2];
                    Line[n++] = Hex[3];
                    Line[n] = 0;
                    IwlLogStage(Line);
                    IwlLogCmdq(Seq);
                    gCmdRead = (gCmdRead + 1) & IWL_CMD_Q_MASK;
                    return -1;
                }
            }
            /*
             * OpenBSD cmd_done：qid bit7=0 且 idx 对上即认。
             * REPLY_ERROR(0x02) 另计；其余一律成功（空 ACK 的 Code 即 opcode）。
             */
            if (!(Rqid & 0x80u) && Ridx == (UINT8)Seq) {
                if (Code == 0x02u) {
                    continue;
                }
                gCmdRead = (gCmdRead + 1) & IWL_CMD_Q_MASK;
                return 0;
            }
            /* 刀 #98：同步等命令时勿丢 RX_MPDU（assoc 后 msg1 会来） */
            if (Code == IWL_RX_MPDU_CMD) {
                IwlRxHoldMpdu(Pkt, RLen);
                continue;
            }
            /* 刀 #45：host 回包（非 bit7）错序则暂存，供 scfg kick 认领 */
            if (!(Rqid & 0x80u) && Code != 0x02u) {
                gRspStash = 1;
                gRspStashCode = Code;
                gRspStashIdx = Ridx;
                gRspStashQid = Rqid;
            }
            if (RxHits <= 3u) {
                char Line[40];
                char Hex[12];
                int n = 0;
                const char *P = "rxmiss c=";
                while (*P) {
                    Line[n++] = *P++;
                }
                HalSerialFormatHex(Hex, Code, 2);
                Line[n++] = Hex[2];
                Line[n++] = Hex[3];
                Line[n++] = ' ';
                Line[n++] = 'i';
                Line[n++] = '=';
                HalSerialFormatHex(Hex, Ridx, 2);
                Line[n++] = Hex[2];
                Line[n++] = Hex[3];
                Line[n++] = ' ';
                Line[n++] = 'q';
                Line[n++] = '=';
                HalSerialFormatHex(Hex, Rqid, 2);
                Line[n++] = Hex[2];
                Line[n++] = Hex[3];
                Line[n++] = ' ';
                Line[n++] = 's';
                Line[n++] = '=';
                HalSerialFormatHex(Hex, Seq & 0xffu, 2);
                Line[n++] = Hex[2];
                Line[n++] = Hex[3];
                Line[n] = 0;
                IwlLogVerb(Line);
            }
        }
        IwlStallMs(1);
    }
    } /* WaitMax */
    gCmdRead = (gCmdRead + 1) & IWL_CMD_Q_MASK;
    {
        char Line[48];
        char Hex[12];
        int n = 0;
        const char *P = "cmdto n=";
        while (*P) {
            Line[n++] = *P++;
        }
        HalSerialFormatHex(Hex, RxHits & 0xffu, 2);
        Line[n++] = Hex[2];
        Line[n++] = Hex[3];
        Line[n++] = ' ';
        Line[n++] = 'c';
        Line[n++] = '=';
        HalSerialFormatHex(Hex, LastCode, 2);
        Line[n++] = Hex[2];
        Line[n++] = Hex[3];
        Line[n++] = ' ';
        Line[n++] = 'o';
        Line[n++] = '=';
        HalSerialFormatHex(Hex, Opcode, 2);
        Line[n++] = Hex[2];
        Line[n++] = Hex[3];
        Line[n++] = ' ';
        Line[n++] = 'r';
        Line[n++] = '=';
        HalSerialFormatHex(Hex, IwlRxDiagClosed() & 0xffu, 2);
        Line[n++] = Hex[2];
        Line[n++] = Hex[3];
        Line[n++] = '/';
        HalSerialFormatHex(Hex, IwlRxDiagRead() & 0xffu, 2);
        Line[n++] = Hex[2];
        Line[n++] = Hex[3];
        Line[n] = 0;
        IwlLogStage(Line);
        IwlLogCmdq(Seq);
    }
    return -1;
}

int IwlSendFrameRaw(const UINT8 *Frame80211, UINTN Len) {
    UINT8 *Buf;
    IWL_TFD *Tfd;
    UINT32 Idx;
    UINT32 Next;
    UINT32 Slot;
    UINT32 Need;
    UINT32 Flags;
    UINT32 Rate;
    UINT16 PmTo;
    UINT8 Subtype;
    UINT8 StaId;
    UINT32 Qid;
    UINTN i;
    UINT64 Phys;

    if (!Frame80211 || Len == 0 || Len > 400 || !gAuxReady || !gAuxBuf) {
        return -1;
    }
    Need = 4u + IWL_TX_CMD_HDR_SIZE + (UINT32)Len;
    if (Need > IWL_AUX_SLOT) {
        return -1;
    }
    /*
     * 刀 #83/#84：TX_CMD 走数据队列；多槽不 Stall。
     * 刀 #126：无 AP 队列时 AUX 强制 AUX_STA。
     * 刀 #127：apqhw 后改走 q5 + AP_STA（BSS data/EAPOL）。
     */
    if (gApQReady) {
        Qid = IWL_DQA_MIN_MGMT_QUEUE;
        StaId = (UINT8)IWL_AP_STA_ID;
    } else {
        Qid = IWL_DQA_AUX_QUEUE;
        StaId = (UINT8)IWL_AUX_STA_ID;
    }
    Idx = gAuxWrite & IWL_TFD_Q_MASK;
    Slot = Idx % IWL_AUX_SLOTS;
    Buf = gAuxBuf + Slot * IWL_AUX_SLOT;
    Phys = gAuxBufPhys + (UINT64)Slot * IWL_AUX_SLOT;
    for (i = 0; i < IWL_AUX_SLOT; i++) {
        Buf[i] = 0;
    }
    Subtype = (UINT8)((Frame80211[0] >> 4) & 0x0fu);
    Flags = IWL_TX_CMD_FLG_ACK | IWL_TX_CMD_FLG_SEQ_CTL
          | IWL_TX_CMD_FLG_BT_DIS;
    Rate = IWL_RATE_1M_PLCP | IWL_RATE_MCS_CCK | IWL_RATE_MCS_ANT_A;
    PmTo = (Subtype == 0u || Subtype == 2u)
           ? (UINT16)IWL_PM_FRAME_ASSOC : (UINT16)IWL_PM_FRAME_MGMT;

    Buf[0] = (UINT8)IWL_CMD_TX;
    Buf[1] = 0;
    Buf[2] = (UINT8)Idx;
    Buf[3] = (UINT8)Qid;
    Buf[4 + 0] = (UINT8)Len;
    Buf[4 + 1] = (UINT8)(Len >> 8);
    Buf[4 + 4] = (UINT8)Flags;
    Buf[4 + 5] = (UINT8)(Flags >> 8);
    Buf[4 + 6] = (UINT8)(Flags >> 16);
    Buf[4 + 7] = (UINT8)(Flags >> 24);
    Buf[4 + 12] = (UINT8)Rate;
    Buf[4 + 13] = (UINT8)(Rate >> 8);
    Buf[4 + 14] = (UINT8)(Rate >> 16);
    Buf[4 + 15] = (UINT8)(Rate >> 24);
    Buf[4 + 16] = StaId;
    Buf[4 + 36] = (UINT8)IWL_TX_CMD_LIFE_INFINITE;
    Buf[4 + 37] = (UINT8)(IWL_TX_CMD_LIFE_INFINITE >> 8);
    Buf[4 + 38] = (UINT8)(IWL_TX_CMD_LIFE_INFINITE >> 16);
    Buf[4 + 39] = (UINT8)(IWL_TX_CMD_LIFE_INFINITE >> 24);
    Buf[4 + 44] = 3;
    Buf[4 + 45] = 7;
    Buf[4 + 46] = (UINT8)IWL_MAX_TID_COUNT;
    Buf[4 + 48] = (UINT8)PmTo;
    Buf[4 + 49] = (UINT8)(PmTo >> 8);
    for (i = 0; i < Len; i++) {
        Buf[4 + IWL_TX_CMD_HDR_SIZE + i] = Frame80211[i];
    }

    Tfd = &gAuxTfd[Idx];
    IwlZero(Tfd, sizeof(*Tfd));
    IwlTfdSetTb(Tfd, 0, Phys, (UINT16)Need);
    IwlFlushDma(Buf, Need);
    IwlFlushDma(Tfd, sizeof(*Tfd));
    IwlUpdateSched(Qid, Idx, StaId, (UINT16)Len);

    Next = (Idx + 1u) & IWL_TFD_Q_MASK;
    gAuxWrite = Next;
    if (!IwlNicLock()) {
        return -1;
    }
    IwlMmioW32(IWL_HBUS_TARG_WRPTR, (Qid << 8) | (Next & 0xffu));
    IwlNicUnlock();
    if (gAuxTxLog < 4u) {
        gAuxTxLog++;
        IwlLogVerb(gApQReady ? "tx=ap5" : (gIwlTxStaId == (UINT8)IWL_AUX_STA_ID
                                          ? "tx=aux" : "tx=aux1"));
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
