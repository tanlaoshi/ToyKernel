/*
 * IwlMvmInternal.h — MVM 跨文件辅助（PR-S-iwl-split-3）
 */
#ifndef IWL_MVM_INTERNAL_H
#define IWL_MVM_INTERNAL_H
#include "IwlPrivate.h"
void IwlMvmZero(void *P, UINTN N);
void IwlMvmCopy(void *D, const void *S, UINTN N);
void IwlMvmPut16(UINT8 *P, UINT16 V);
void IwlMvmPut32(UINT8 *P, UINT32 V);
void IwlMvmLogCmdFail(const char *Tag, UINT32 Op);
int IwlMvmDqaEnable(void);
int IwlMvmPhyCtxt(void);
int IwlMvmAuxSta(void);
int IwlMacCtxtSend(UINT32 Action, int IsAssoc, int Sync, int FwDecrypt);
int IwlBindingAdd(void);
int IwlSendLq(UINT8 StaId);
int IwlStaEnableTx(UINT8 StaId);
int IwlAddStaKey(UINT8 KeyOff, UINT16 Flags, const UINT8 Key[16]);
int IwlMvmScanCfg(void);
int IwlMvmMcc(void);
int IwlMvmAntCanary(void);
#endif
