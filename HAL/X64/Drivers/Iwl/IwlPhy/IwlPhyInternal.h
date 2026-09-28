/*
 * IwlPhyInternal.h — PHY DB 跨文件（PR-S-iwl-split-5）
 */
#ifndef IWL_PHY_INTERNAL_H
#define IWL_PHY_INTERNAL_H
#include "IwlPrivate.h"

#define IWL_PHY_CFG_MAX     512u
#define IWL_PHY_NCH_MAX     2048u
#define IWL_PHY_CHG_MAX     4096u
#define IWL_PHY_SEC_MAX     4096u
#define IWL_NUM_CH_GROUPS   9u

extern UINT8 gPhyCfgData[IWL_PHY_CFG_MAX];
extern UINT16 gPhyCfgLen;
extern UINT8 gPhyNchData[IWL_PHY_NCH_MAX];
extern UINT16 gPhyNchLen;
extern UINT8 gPapdData[IWL_NUM_CH_GROUPS][IWL_PHY_CHG_MAX];
extern UINT16 gPapdLen[IWL_NUM_CH_GROUPS];
extern UINT8 gTxpData[IWL_NUM_CH_GROUPS][IWL_PHY_CHG_MAX];
extern UINT16 gTxpLen[IWL_NUM_CH_GROUPS];
extern UINT32 gPhyNotifN;
extern UINT32 gPapdN;
extern UINT32 gTxpN;
extern UINT32 gPhyDropN;
extern UINT8 gCalTypes[12];
extern UINT16 gCalLens[12];
extern UINT32 gCalDumpN;

void IwlPhyZero(void *P, UINTN N);
void IwlPhyCopy(void *D, const void *S, UINTN N);
void IwlPhyPut16(UINT8 *P, UINT16 V);
void IwlPhyPut32(UINT8 *P, UINT32 V);
int IwlPhySendCfg(UINT32 UcodeType);
int IwlPhySendAnt(void);
void IwlPhyStoreSection(UINT16 Type, const UINT8 *Data, UINT16 Len);
void IwlPhyHandleRx(IWL_RX_PKT *Pkt, UINTN Len, int *GotInit);
int IwlPhyDbSendOne(UINT16 Type, const UINT8 *Data, UINT16 Len);
#endif
