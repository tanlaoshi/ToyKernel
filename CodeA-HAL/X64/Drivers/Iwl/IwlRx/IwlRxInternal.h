/*
 * IwlRxInternal.h — RX Hold 跨文件（PR-S-iwl-split-5）
 */
#ifndef IWL_RX_INTERNAL_H
#define IWL_RX_INTERNAL_H
#include "IwlPrivate.h"

#define IWL_MPDU_HOLD_N   24u
#define IWL_MPDU_HOLD_MAX 512u

extern UINT8 gMpduHold[IWL_MPDU_HOLD_N][IWL_MPDU_HOLD_MAX];
extern UINT16 gMpduHoldLen[IWL_MPDU_HOLD_N];
extern UINT8 gMpduHoldR;
extern UINT8 gMpduHoldW;
extern UINT8 gMpduHoldCnt;

int IwlHoldSkipMpdu(const UINT8 *Slot, UINT16 SlotLen);
#endif
