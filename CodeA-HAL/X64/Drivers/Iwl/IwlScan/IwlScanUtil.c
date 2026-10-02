/*
 * IwlScanUtil.c — 扫描小工具（PR-S-iwl-split-4）
 */
#include "IwlScanInternal.h"
#include "HalSerial.h"

UINT8 gLastPhyChan;

void IwlScanZero(void *P, UINTN N) {
    UINT8 *B = (UINT8 *)P;
    UINTN i;
    for (i = 0; i < N; i++) {
        B[i] = 0;
    }
}

void IwlScanCopyN(UINT8 *D, const UINT8 *S, UINTN N) {
    UINTN i;
    for (i = 0; i < N; i++) {
        D[i] = S[i];
    }
}

int IwlScanStrEq(const char *A, const UINT8 *B, UINTN Bl) {
    UINTN i;
    for (i = 0; i < Bl; i++) {
        if (A[i] == 0 || (UINT8)A[i] != B[i]) {
            return 0;
        }
    }
    return A[Bl] == 0;
}

void IwlScanPut16(UINT8 *P, UINT16 V) {
    P[0] = (UINT8)V;
    P[1] = (UINT8)(V >> 8);
}

void IwlScanPut32(UINT8 *P, UINT32 V) {
    P[0] = (UINT8)V;
    P[1] = (UINT8)(V >> 8);
    P[2] = (UINT8)(V >> 16);
    P[3] = (UINT8)(V >> 24);
}

