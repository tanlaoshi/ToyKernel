/*
 * IwlAssocUtil.c — 关联小工具（PR-S-iwl-split-4）
 */
#include "IwlAssocInternal.h"
#include "HalSerial.h"

void IwlAssocCopyN(UINT8 *D, const UINT8 *S, UINTN N) {
    UINTN i;
    for (i = 0; i < N; i++) {
        D[i] = S[i];
    }
}

void IwlBuildMgmt(UINT8 *Out, UINTN *OutLen, UINT8 Subtype,
                         const UINT8 *Payload, UINTN PayLen) {
    UINTN i;
    Out[0] = (UINT8)(0x00 | (Subtype << 4));
    Out[1] = 0x00;
    Out[2] = 0;
    Out[3] = 0;
    IwlAssocCopyN(Out + 4, gIwlTarget.Bssid, 6);
    IwlAssocCopyN(Out + 10, gIwlMac, 6);
    IwlAssocCopyN(Out + 16, gIwlTarget.Bssid, 6);
    Out[22] = 0;
    Out[23] = 0;
    for (i = 0; i < PayLen; i++) {
        Out[24 + i] = Payload[i];
    }
    *OutLen = 24 + PayLen;
}

/* 刀 #102：live + Hold 一起抽（phy Sync 会把 MPDU 塞进 Hold） */
int IwlAssocTake(IWL_RX_PKT **Pkt, UINTN *Len) {
    if (IwlRxTakeHeld(Pkt, Len)) {
        return 1;
    }
    return IwlRxTake(Pkt, Len);
}
