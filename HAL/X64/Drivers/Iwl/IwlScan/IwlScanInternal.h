/*
 * IwlScanInternal.h — 扫描跨文件（PR-S-iwl-split-4）
 */
#ifndef IWL_SCAN_INTERNAL_H
#define IWL_SCAN_INTERNAL_H
#include "IwlPrivate.h"

#define IWL_SCAN_REQ_MAX 2048
#define IWL_SCAN_REQ_UMAC_SIZE_V7 48
#define IWL_SCAN_ADWELL_BUDGET_FULL 300
#define IWL_SCAN_ADWELL_N_APS 2
#define IWL_SCAN_ADWELL_N_APS_SOCIAL 10
extern UINT8 gLastPhyChan;
void IwlScanZero(void *P, UINTN N);
void IwlScanCopyN(UINT8 *D, const UINT8 *S, UINTN N);
int IwlScanStrEq(const char *A, const UINT8 *B, UINTN Bl);
void IwlScanPut16(UINT8 *P, UINT16 V);
void IwlScanPut32(UINT8 *P, UINT32 V);
void IwlScanCopyVis(char *Dst, const UINT8 *Src, UINTN Len);
int IwlParseBeacon(const UINT8 *Frame, UINTN Len);
void IwlNoteBeacon(const UINT8 *Frame, UINTN Len, char *Heard);
int IwlBuildUmacScan(UINT8 *Req, UINT32 *OutLen);
int IwlScanCollect(void);
#endif
