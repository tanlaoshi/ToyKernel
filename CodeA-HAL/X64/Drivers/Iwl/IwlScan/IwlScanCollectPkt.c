/*
 * IwlScanCollectPkt.c — 扫描收包单帧（PR-F-iwl-2）
 */
#include "IwlScanInternal.h"
#include "HalSerial.h"

void IwlScanCollectCtxInit(IWL_SCAN_COLLECT_CTX *C) {
    C->Ncode = 0;
    C->GotAck = 1;
    C->GotDone = 0;
    C->StopAt = -1;
    C->BeaconN = 0;
    C->FirstFc = 0;
    C->Heard[0] = 0;
}

/* 1 = 命中目标 SSID（调用方应 return 1）；0 = 继续 */
int IwlScanCollectOnPkt(IWL_SCAN_COLLECT_CTX *C, IWL_RX_PKT *Pkt, UINTN Len,
                        UINT32 LoopI) {
    const UINT8 *Raw = (const UINT8 *)&Pkt->Hdr;
    UINT8 Code = Pkt->Hdr.Code;
    UINT8 Flags = Pkt->Hdr.Flags;
    UINT8 Qid = Pkt->Hdr.Qid;
    int IsNotif = (Qid & 0x80u) != 0;
    UINTN HdrSz = (Len >= sizeof(IWL_CMD_HDR_WIDE) && Flags
                   && Len != sizeof(IWL_CMD_HDR))
                      ? sizeof(IWL_CMD_HDR_WIDE)
                      : sizeof(IWL_CMD_HDR);
    const UINT8 *Payload = Raw + HdrSz;
    UINTN PayLen = Len > HdrSz ? Len - HdrSz : 0;

    gIwlScanCount++;
    if (C->Ncode < 8) {
        C->Codes[C->Ncode++] = Code | (IsNotif ? 0x100u : 0);
    }
    if (gIwlScanCount <= 8) {
        char Line[32];
        char Hex[12];
        int N = 0;
        const char *P = IsNotif ? "rxntf c=" : "rxack c=";
        while (*P) {
            Line[N++] = *P++;
        }
        HalSerialFormatHex(Hex, Code, 2);
        Line[N++] = Hex[2];
        Line[N++] = Hex[3];
        Line[N++] = ' ';
        Line[N++] = 'i';
        Line[N++] = '=';
        HalSerialFormatHex(Hex, Pkt->Hdr.Idx, 2);
        Line[N++] = Hex[2];
        Line[N++] = Hex[3];
        Line[N] = 0;
        IwlLogVerb(Line);
    }
    if (gIwlScanCount <= 4) {
        char Line[56];
        char Hex[12];
        int N = 0;
        UINT32 K;
        const char *P = "rxraw L=";
        while (*P) {
            Line[N++] = *P++;
        }
        HalSerialFormatHex(Hex, (UINT32)Len, 4);
        Line[N++] = Hex[2];
        Line[N++] = Hex[3];
        Line[N++] = Hex[4];
        Line[N++] = Hex[5];
        Line[N++] = ' ';
        Line[N++] = 'f';
        Line[N++] = '=';
        HalSerialFormatHex(Hex, Flags, 2);
        Line[N++] = Hex[2];
        Line[N++] = Hex[3];
        Line[N++] = ' ';
        for (K = 0; K < 16 && N < 52; K++) {
            HalSerialFormatHex(Hex, Raw[K], 2);
            Line[N++] = Hex[2];
            Line[N++] = Hex[3];
        }
        Line[N] = 0;
        IwlLogVerb(Line);
    }
    if (!IsNotif && Code == IWL_CMD_SCAN_REQ_UMAC) {
        UINT32 St = 0;
        if (Len >= 4) {
            St = (UINT32)Raw[Len - 4] | ((UINT32)Raw[Len - 3] << 8)
               | ((UINT32)Raw[Len - 2] << 16) | ((UINT32)Raw[Len - 1] << 24);
        }
        C->GotAck = 1;
        if (St != 0) {
            char Line[24];
            char Hex[12];
            int N = 0;
            const char *P = "scan=st ";
            while (*P) {
                Line[N++] = *P++;
            }
            HalSerialFormatHex(Hex, St, 8);
            Line[N++] = Hex[2];
            Line[N++] = Hex[3];
            Line[N++] = Hex[4];
            Line[N++] = Hex[5];
            Line[N++] = Hex[6];
            Line[N++] = Hex[7];
            Line[N++] = Hex[8];
            Line[N++] = Hex[9];
            Line[N] = 0;
            IwlLogStage(Line);
        } else {
            IwlLogStage("scan=ack");
        }
    }
    if (Code == 0x02u && PayLen >= 12) {
        char Line[48];
        char Hex[12];
        int N = 0;
        const char *P = "scerr e=";
        UINT32 Et = (UINT32)Payload[0] | ((UINT32)Payload[1] << 8)
                  | ((UINT32)Payload[2] << 16) | ((UINT32)Payload[3] << 24);
        while (*P) {
            Line[N++] = *P++;
        }
        HalSerialFormatHex(Hex, Et, 8);
        Line[N++] = Hex[2];
        Line[N++] = Hex[3];
        Line[N++] = Hex[4];
        Line[N++] = Hex[5];
        Line[N++] = Hex[6];
        Line[N++] = Hex[7];
        Line[N++] = Hex[8];
        Line[N++] = Hex[9];
        Line[N++] = ' ';
        Line[N++] = 'o';
        Line[N++] = '=';
        HalSerialFormatHex(Hex, Payload[4], 2);
        Line[N++] = Hex[2];
        Line[N++] = Hex[3];
        Line[N] = 0;
        IwlLogStage(Line);
        IwlCmdqSnap();
    }
    if (Code == 0xc0u && PayLen >= 24) {
        /* RX_PHY：channel 在 phy_info+22（le16） */
        gLastPhyChan = Payload[22];
    }
    /*
     * 刀 #81：#80 ack+C1+done 但 fail。8000 RX_MPDU =
     * iwl_rx_mpdu_res_start(4) + 802.11；旧试 +0/+16 漏 +4。
     * rxraw: …8101000C8000… → +4 起 FC=0x80 beacon。
     */
    if (Code == IWL_RX_MPDU_CMD || Code == 0xc3
        || Code == IWL_BEACON_NOTIFICATION) {
        const UINT8 *Frame = Payload;
        UINTN FLen = PayLen;
        if (Code == IWL_RX_MPDU_CMD && PayLen > 4) {
            Frame = Payload + 4;
            FLen = PayLen - 4;
        }
        if (Code == IWL_RX_MPDU_CMD && FLen > 0 && C->FirstFc == 0) {
            C->FirstFc = Frame[0];
        }
        if (FLen > 32
            && ((Frame[0] & 0xFCu) == 0x80u || (Frame[0] & 0xFCu) == 0x50u)) {
            C->BeaconN++;
            IwlNoteBeacon(Frame, FLen, C->Heard);
        }
        if (FLen > 32 && IwlParseBeacon(Frame, FLen)) {
            IwlLogStage("scan=ok");
            return 1;
        }
    }
    if (Code == IWL_SCAN_START_UMAC) {
        IwlLogVerb("scan=start");
    } else if (Code == IWL_SCAN_COMPLETE_UMAC || Code == IWL_SCAN_COMPLETE_LMAC
               || Code == IWL_SCAN_ITERATION_COMPLETE_UMAC) {
        C->GotDone = 1;
        if (gIwlSsidOk) {
            IwlLogStage("scan=ok");
            return 1;
        }
        /* 完成通知后面可能还有信标，先把本轮队列收完 */
        if (C->StopAt < 0) {
            C->StopAt = (int)LoopI + 400;
        }
    }
    return 0;
}
