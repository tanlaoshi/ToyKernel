/*
 * IwlTxInternal.h — TX 队列跨文件状态（PR-S-iwl-split-2，仅 IwlTx*.c 包含）
 */
#ifndef IWL_TX_INTERNAL_H
#define IWL_TX_INTERNAL_H

#include "IwlPrivate.h"

#define IWL_AUX_SLOTS  16u
#define IWL_AUX_SLOT   512u

extern IWL_TFD *gCmdTfd;
extern UINT64 gCmdTfdPhys;
extern UINT8 *gCmdBufs[IWL_CMD_Q_SIZE];
extern UINT64 gCmdBufPhys[IWL_CMD_Q_SIZE];
extern IWL_TFD *gAuxTfd;
extern UINT64 gAuxTfdPhys;
extern IWL_TFD *gApTfd;
extern UINT64 gApTfdPhys;
extern UINT8 *gAuxBuf;
extern UINT64 gAuxBufPhys;
extern UINT32 gAuxWrite;
extern UINT32 gApWrite;
extern UINT32 gAuxTxLog;
extern int gApQReady;
extern int gEapAuxLogged;
extern UINT8 *gFirstTbBase;
extern UINT64 gFirstTbPhys;
extern UINT8 *gKwPage;
extern UINT64 gKwPhys;
extern UINT16 *gBcTbl;
extern UINT64 gBcPhys;
extern UINT32 gSchedBase;
extern UINT32 gCmdWrite;
extern UINT32 gCmdRead;
extern int gTxReady;
extern int gPostAliveOk;
extern int gAuxReady;
extern int gRspStash;
extern UINT8 gRspStashCode;
extern UINT8 gRspStashIdx;
extern UINT8 gRspStashQid;

void IwlTxZero(void *P, UINTN N);
void IwlTxCopy(void *D, const void *S, UINTN N);
void IwlWriteMem32(UINT32 Addr, UINT32 Val);
void IwlUpdateSched(UINT32 Qid, UINT32 Idx, UINT8 StaId, UINT16 Len);
void IwlTfdSetTb(IWL_TFD *Tfd, UINT8 Idx, UINT64 Phys, UINT16 Len);
void IwlLogCmdq(UINT32 Seq);
int IwlEnableAcTxq(UINT32 Qid, UINT32 Fifo);
int IwlEnableCmdTxq(void);
int IwlFhTxDrain(void);
void IwlFhTxStart(void);

/* PR-F-iwl-3：IwlSendCmd 同步等回 */
int IwlSendCmdPollOnce(UINT32 Seq, UINT32 Opcode, UINT8 *LastCode,
                       UINT32 *RxHits);
void IwlSendCmdLogTimeout(UINT32 Seq, UINT32 Opcode, UINT8 LastCode,
                          UINT32 RxHits);

#endif
