/*
 * IwlRxHold.c — MPDU Hold 环（PR-S-iwl-split-5）
 */
#include "IwlRxInternal.h"

UINT8 gMpduHold[IWL_MPDU_HOLD_N][IWL_MPDU_HOLD_MAX];
UINT16 gMpduHoldLen[IWL_MPDU_HOLD_N];
UINT8 gMpduHoldR;
UINT8 gMpduHoldW;
UINT8 gMpduHoldCnt;

int IwlHoldSkipMpdu(const UINT8 *Slot, UINT16 SlotLen) {
    const UINT8 *Dot;
    UINTN Off;
    UINT8 Fc0;
    UINT8 Type;
    UINT8 Sub;

    if (SlotLen < sizeof(IWL_CMD_HDR) + 4u + 2u) {
        return 0;
    }
    /* Slot: LenNFlags(4) + IWL_CMD_HDR + RX_MPDU payload(desc+frame) */
    Off = 4u + sizeof(IWL_CMD_HDR) + 4u;
    if (SlotLen < Off + 2u) {
        return 0;
    }
    Dot = Slot + Off;
    Fc0 = Dot[0];
    Type = (UINT8)((Fc0 >> 2) & 0x3u);
    Sub = (UINT8)((Fc0 >> 4) & 0x0Fu);
    if (Type == 0u && (Sub == 8u || Sub == 5u)) {
        return 1; /* beacon / probe-resp */
    }
    /* 刀 #139：公司 assoc 窗被 Null(0x48)/QoS-Null(0xC8) 灌满 */
    if (Type == 2u && (Sub == 4u || Sub == 12u)) {
        return 1;
    }
    return 0;
}
void IwlRxHoldMpdu(const IWL_RX_PKT *Pkt, UINTN Len) {
    UINTN Copy;
    UINTN i;
    UINT8 *Dst;
    UINT8 Tmp[IWL_MPDU_HOLD_MAX];
    UINT16 TmpLen;

    if (!Pkt || Len < sizeof(IWL_CMD_HDR) || Pkt->Hdr.Code != IWL_RX_MPDU_CMD) {
        return;
    }
    Copy = sizeof(UINT32) + Len;
    if (Copy > IWL_MPDU_HOLD_MAX) {
        Copy = IWL_MPDU_HOLD_MAX;
    }
    Tmp[0] = (UINT8)Pkt->LenNFlags;
    Tmp[1] = (UINT8)(Pkt->LenNFlags >> 8);
    Tmp[2] = (UINT8)(Pkt->LenNFlags >> 16);
    Tmp[3] = (UINT8)(Pkt->LenNFlags >> 24);
    for (i = 4; i < Copy; i++) {
        Tmp[i] = ((const UINT8 *)Pkt)[i];
    }
    TmpLen = (UINT16)(Copy - sizeof(UINT32));

    /* #130/#139：beacon/probe/Null 不占握手槽 */
    if (IwlHoldSkipMpdu(Tmp, (UINT16)Copy)) {
        return;
    }

    if (gMpduHoldCnt >= IWL_MPDU_HOLD_N) {
        gMpduHoldR = (UINT8)((gMpduHoldR + 1u) % IWL_MPDU_HOLD_N);
        gMpduHoldCnt--;
    }
    Dst = gMpduHold[gMpduHoldW];
    for (i = 0; i < Copy; i++) {
        Dst[i] = Tmp[i];
    }
    gMpduHoldLen[gMpduHoldW] = TmpLen;
    gMpduHoldW = (UINT8)((gMpduHoldW + 1u) % IWL_MPDU_HOLD_N);
    gMpduHoldCnt++;
}

UINT8 IwlRxHoldCount(void) {
    return gMpduHoldCnt;
}

/* 刀 #176：post=sta 窗内 AP 可能已塞进旧 M1；握手前清空，迫使用 Start 后的新帧 */
void IwlRxHoldFlush(void) {
    gMpduHoldR = 0;
    gMpduHoldW = 0;
    gMpduHoldCnt = 0;
}

int IwlRxTakeHeld(IWL_RX_PKT **OutPkt, UINTN *OutLen) {
    if (!OutPkt || !OutLen || gMpduHoldCnt == 0) {
        return 0;
    }
    *OutPkt = (IWL_RX_PKT *)gMpduHold[gMpduHoldR];
    *OutLen = gMpduHoldLen[gMpduHoldR];
    gMpduHoldR = (UINT8)((gMpduHoldR + 1u) % IWL_MPDU_HOLD_N);
    gMpduHoldCnt--;
    return 1;
}
