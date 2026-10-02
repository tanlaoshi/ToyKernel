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

/* PR-F-iwl-2：IwlScanCollect 拆分 */
typedef struct {
    UINT32 Codes[8];
    UINT32 Ncode;
    int GotAck;
    int GotDone;
    int StopAt;
    UINT32 BeaconN;
    UINT8 FirstFc;
    char Heard[9];
} IWL_SCAN_COLLECT_CTX;

void IwlScanCollectCtxInit(IWL_SCAN_COLLECT_CTX *C);
int IwlScanCollectOnPkt(IWL_SCAN_COLLECT_CTX *C, IWL_RX_PKT *Pkt, UINTN Len,
                        UINT32 LoopI);
void IwlScanCollectLogFail(const IWL_SCAN_COLLECT_CTX *C);
#endif
