/*
 * IwlTx.c — TX 状态 / 辅助 / TxInit / NicInit（PR-S-iwl-split-2）
 */
#include "IwlTxInternal.h"
#include "PhysicalMemory.h"
#include "HalSerial.h"

IWL_TFD *gCmdTfd;

UINT64 gCmdTfdPhys;

UINT8 *gCmdBufs[IWL_CMD_Q_SIZE];

UINT64 gCmdBufPhys[IWL_CMD_Q_SIZE];

IWL_TFD *gAuxTfd;

UINT64 gAuxTfdPhys;

IWL_TFD *gApTfd;

UINT64 gApTfdPhys;

/* 刀 #84：多槽 AUX 缓冲，避免等 RDPTR 时在 LwIp CLI 锁里 Stall→键鼠假死 */

UINT8 *gAuxBuf;

UINT64 gAuxBufPhys;

UINT32 gAuxWrite;

UINT32 gApWrite; /* #149：q5 从 0 起，不跟 AUX 写指针 */

UINT32 gAuxTxLog;

int gApQReady; /* #127：q5 HW 已绑 */

int gEapAuxLogged;

UINT8 *gFirstTbBase; /* 32 × 64B：Linux FIRST_TB 双向 DMA 窗 */

UINT64 gFirstTbPhys;

UINT8 *gKwPage;

UINT64 gKwPhys;

UINT16 *gBcTbl; /* [qid][idx] 展平：qid * IWL_TFD_BC_SIZE + idx */

UINT64 gBcPhys;

UINT32 gSchedBase;

UINT32 gCmdWrite;

UINT32 gCmdRead;

int gTxReady;

int gPostAliveOk;

int gAuxReady;

/* 刀 #45：错序 host 回包暂存（scfg 被下一命令撞出时不被 rxmiss 丢掉） */

int gRspStash;

UINT8 gRspStashCode;

UINT8 gRspStashIdx;

UINT8 gRspStashQid;

void IwlTxZero(void *P, UINTN N) {
    UINT8 *B = (UINT8 *)P;
    UINTN i;
    for (i = 0; i < N; i++) {
        B[i] = 0;
    }
}

void IwlTxCopy(void *D, const void *S, UINTN N) {
    UINT8 *d = (UINT8 *)D;
    const UINT8 *s = (const UINT8 *)S;
    UINTN i;
    for (i = 0; i < N; i++) {
        d[i] = s[i];
    }
}

void IwlWriteMem32(UINT32 Addr, UINT32 Val) {
    IwlMmioW32(IWL_HBUS_TARG_MEM_WADDR, Addr);
    IwlMmioW32(IWL_HBUS_TARG_MEM_WDAT, Val);
}

void IwlUpdateSched(UINT32 Qid, UINT32 Idx, UINT8 StaId, UINT16 Len) {
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

void IwlTfdSetTb(IWL_TFD *Tfd, UINT8 Idx, UINT64 Phys, UINT16 Len) {
    Tfd->Tb[Idx].Lo = (UINT32)Phys;
    Tfd->Tb[Idx].HiNLen = (UINT16)(((Phys >> 32) & 0xFu)
                                   | ((Len & 0xfffu) << 4));
    Tfd->NumTbs = (UINT8)(Idx + 1u);
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
        IwlTxZero(Mem, PAGE_SIZE);
        gKwPage = (UINT8 *)Mem;
        gKwPhys = (UINT64)(UINTN)Mem;

        /* 刀 #33：256 × 128B TFD = 8 页（HW 环长，非 32） */
        Mem = PhysicalMemoryAllocatePages(8);
        if (!Mem) {
            return 0;
        }
        IwlTxZero(Mem, 8 * PAGE_SIZE);
        gCmdTfd = (IWL_TFD *)Mem;
        gCmdTfdPhys = (UINT64)(UINTN)Mem;

        for (i = 0; i < IWL_CMD_Q_SIZE; i++) {
            Mem = PhysicalMemoryAllocatePages(1);
            if (!Mem) {
                return 0;
            }
            IwlTxZero(Mem, PAGE_SIZE);
            gCmdBufs[i] = (UINT8 *)Mem;
            gCmdBufPhys[i] = (UINT64)(UINTN)Mem;
        }

        /* FIRST_TB：32 × 64B 装一页（Linux bidirectional TB0） */
        Mem = PhysicalMemoryAllocatePages(1);
        if (!Mem) {
            return 0;
        }
        IwlTxZero(Mem, PAGE_SIZE);
        gFirstTbBase = (UINT8 *)Mem;
        gFirstTbPhys = (UINT64)(UINTN)Mem;

        /* 刀 #83/#84：AUX TFD 环 + 16×512B 槽（数据面不 Stall） */
        Mem = PhysicalMemoryAllocatePages(8);
        if (!Mem) {
            return 0;
        }
        IwlTxZero(Mem, 8 * PAGE_SIZE);
        gAuxTfd = (IWL_TFD *)Mem;
        gAuxTfdPhys = (UINT64)(UINTN)Mem;
        Mem = PhysicalMemoryAllocatePages(8);
        if (!Mem) {
            return 0;
        }
        IwlTxZero(Mem, 8 * PAGE_SIZE);
        gApTfd = (IWL_TFD *)Mem;
        gApTfdPhys = (UINT64)(UINTN)Mem;
        Mem = PhysicalMemoryAllocatePages(2);
        if (!Mem) {
            return 0;
        }
        IwlTxZero(Mem, 2 * PAGE_SIZE);
        gAuxBuf = (UINT8 *)Mem;
        gAuxBufPhys = (UINT64)(UINTN)Mem;
        gAuxTxLog = 0;

        /* BC 表：31 queues × 320 × u16 ≈ 20KB → 6 页（1KB 对齐） */
        BcPages = 6;
        Mem = PhysicalMemoryAllocatePages(BcPages);
        if (!Mem) {
            return 0;
        }
        IwlTxZero(Mem, BcPages * PAGE_SIZE);
        gBcTbl = (UINT16 *)Mem;
        gBcPhys = (UINT64)(UINTN)Mem;
        gTxReady = 1;
    }
    gCmdWrite = 0;
    gCmdRead = 0;
    gAuxWrite = 0;
    gApWrite = 0;
    gPostAliveOk = 0;
    gAuxReady = 0;
    gApQReady = 0;
    gEapAuxLogged = 0;

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
    IwlFlushDma(gApTfd, 8 * PAGE_SIZE);
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
