/*
 * IwlAssocInternal.h — 关联跨文件（PR-S-iwl-split-4）
 */
#ifndef IWL_ASSOC_INTERNAL_H
#define IWL_ASSOC_INTERNAL_H
#include "IwlPrivate.h"
void IwlAssocCopyN(UINT8 *D, const UINT8 *S, UINTN N);
void IwlBuildMgmt(UINT8 *Out, UINTN *OutLen, UINT8 Subtype,
                  const UINT8 *Pay, UINTN PayLen);
int IwlAssocTake(IWL_RX_PKT **Pkt, UINTN *Len);
UINTN IwlBuildStaRsn(UINT8 *Out, UINTN Cap);
int IwlAssocWaitAuth(UINT32 *RxN, UINT8 *FirstFc);
int IwlAssocWaitResp(UINT32 *RxN, UINT8 *FirstFc, int *GotAssoc);
#endif
