/*
 * IwlEapolInternal.h — EAPOL M1 跨文件（PR-F-iwl-1）
 */
#ifndef IWL_EAPOL_INTERNAL_H
#define IWL_EAPOL_INTERNAL_H

#include "IwlPrivate.h"

typedef struct {
    UINT32 RxMpdu;
    UINT32 RxData;
    UINT32 RxUni;
    UINT32 EapHit;
    UINT16 LastKi;
    UINT8 FirstFc0;
    UINT8 FirstFc1;
    UINT8 UniFc0;
    UINT8 UniFc1;
    UINT8 UniSnap[8];
    int UniSnapOk;
    UINT8 DataDa[6];
    int DataDaOk;
    UINT8 Start[4];
    UINT32 M1At;
    int M1Fresh;
    int Got1;
    UINT16 KeyInfo;
    UINT8 KeyDesc;
} IWL_EAPOL_M1_CTX;

void IwlEapolM1CtxInit(IWL_EAPOL_M1_CTX *C);
void IwlEapolM1SendStart(IWL_EAPOL_M1_CTX *C);
/* 1 = 收到 deauth/disassoc，应结束等待；0 = 继续 */
int IwlEapolM1DrainRx(IWL_EAPOL_M1_CTX *C, UINT8 *Eapol, UINTN *EapLen,
                      UINT8 *Anonce, UINT8 *Replay, UINT32 LoopI);
void IwlEapolM1LogTimeout(const IWL_EAPOL_M1_CTX *C);

#endif /* IWL_EAPOL_INTERNAL_H */
