/*
 * IwlScanCollect.c — 扫描收包环（PR-S-iwl-split-4）
 */
#include "IwlScanInternal.h"
#include "HalSerial.h"

int IwlScanCollect(void) {
    UINT32 i;
    IWL_RX_PKT *Pkt;
    UINTN Len;
    UINT32 Codes[8];
    UINT32 Ncode = 0;
    int GotAck = 1;
    int GotDone = 0;
    int StopAt = -1;
    UINT32 BeaconN = 0;
    UINT8 FirstFc = 0;
    char Heard[9];

    Heard[0] = 0;
    for (i = 0; i < 8000; i++) {
        IwlRxPoll();
        while (IwlRxTake(&Pkt, &Len)) {
            const UINT8 *Raw = (const UINT8 *)&Pkt->Hdr;
            UINT8 Code = Pkt->Hdr.Code;
            UINT8 Flags = Pkt->Hdr.Flags;
            UINT8 Qid = Pkt->Hdr.Qid;
            int IsNotif = (Qid & 0x80u) != 0;
            UINTN HdrSz = (Len >= sizeof(IWL_CMD_HDR_WIDE) && Flags
                           && Len != sizeof(IWL_CMD_HDR))
                          ? sizeof(IWL_CMD_HDR_WIDE) : sizeof(IWL_CMD_HDR);
            const UINT8 *Payload = Raw + HdrSz;
            UINTN PayLen = Len > HdrSz ? Len - HdrSz : 0;
            gIwlScanCount++;
            if (Ncode < 8) {
                Codes[Ncode++] = Code | (IsNotif ? 0x100u : 0);
            }
            if (gIwlScanCount <= 8) {
                char Line[32];
                char Hex[12];
                int n = 0;
                const char *P = IsNotif ? "rxntf c=" : "rxack c=";
                while (*P) {
                    Line[n++] = *P++;
                }
                HalSerialFormatHex(Hex, Code, 2);
                Line[n++] = Hex[2];
                Line[n++] = Hex[3];
                Line[n++] = ' ';
                Line[n++] = 'i';
                Line[n++] = '=';
                HalSerialFormatHex(Hex, Pkt->Hdr.Idx, 2);
                Line[n++] = Hex[2];
                Line[n++] = Hex[3];
                Line[n] = 0;
                IwlLogVerb(Line);
            }
            if (gIwlScanCount <= 4) {
                char Line[56];
                char Hex[12];
                int n = 0;
                UINT32 k;
                const char *P = "rxraw L=";
                while (*P) {
                    Line[n++] = *P++;
                }
                HalSerialFormatHex(Hex, (UINT32)Len, 4);
                Line[n++] = Hex[2];
                Line[n++] = Hex[3];
                Line[n++] = Hex[4];
                Line[n++] = Hex[5];
                Line[n++] = ' ';
                Line[n++] = 'f';
                Line[n++] = '=';
                HalSerialFormatHex(Hex, Flags, 2);
                Line[n++] = Hex[2];
                Line[n++] = Hex[3];
                Line[n++] = ' ';
                for (k = 0; k < 16 && n < 52; k++) {
                    HalSerialFormatHex(Hex, Raw[k], 2);
                    Line[n++] = Hex[2];
                    Line[n++] = Hex[3];
                }
                Line[n] = 0;
                IwlLogVerb(Line);
            }
            if (!IsNotif && Code == IWL_CMD_SCAN_REQ_UMAC) {
                UINT32 St = 0;
                if (Len >= 4) {
                    St = (UINT32)Raw[Len - 4]
                       | ((UINT32)Raw[Len - 3] << 8)
                       | ((UINT32)Raw[Len - 2] << 16)
                       | ((UINT32)Raw[Len - 1] << 24);
                }
                GotAck = 1;
                if (St != 0) {
                    char Line[24];
                    char Hex[12];
                    int n = 0;
                    const char *P = "scan=st ";
                    while (*P) {
                        Line[n++] = *P++;
                    }
                    HalSerialFormatHex(Hex, St, 8);
                    Line[n++] = Hex[2];
                    Line[n++] = Hex[3];
                    Line[n++] = Hex[4];
                    Line[n++] = Hex[5];
                    Line[n++] = Hex[6];
                    Line[n++] = Hex[7];
                    Line[n++] = Hex[8];
                    Line[n++] = Hex[9];
                    Line[n] = 0;
                    IwlLogStage(Line);
                } else {
                    IwlLogStage("scan=ack");
                }
            }
            if (Code == 0x02u && PayLen >= 12) {
                char Line[48];
                char Hex[12];
                int n = 0;
                const char *P = "scerr e=";
                UINT32 Et = (UINT32)Payload[0] | ((UINT32)Payload[1] << 8)
                          | ((UINT32)Payload[2] << 16)
                          | ((UINT32)Payload[3] << 24);
                while (*P) {
                    Line[n++] = *P++;
                }
                HalSerialFormatHex(Hex, Et, 8);
                Line[n++] = Hex[2];
                Line[n++] = Hex[3];
                Line[n++] = Hex[4];
                Line[n++] = Hex[5];
                Line[n++] = Hex[6];
                Line[n++] = Hex[7];
                Line[n++] = Hex[8];
                Line[n++] = Hex[9];
                Line[n++] = ' ';
                Line[n++] = 'o';
                Line[n++] = '=';
                HalSerialFormatHex(Hex, Payload[4], 2);
                Line[n++] = Hex[2];
                Line[n++] = Hex[3];
                Line[n] = 0;
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
                if (Code == IWL_RX_MPDU_CMD && FLen > 0 && FirstFc == 0) {
                    FirstFc = Frame[0];
                }
                if (FLen > 32 &&
                    ((Frame[0] & 0xFCu) == 0x80u || (Frame[0] & 0xFCu) == 0x50u)) {
                    BeaconN++;
                    IwlNoteBeacon(Frame, FLen, Heard);
                }
                if (FLen > 32 && IwlParseBeacon(Frame, FLen)) {
                    IwlLogStage("scan=ok");
                    return 1;
                }
            }
            if (Code == IWL_SCAN_START_UMAC) {
                IwlLogVerb("scan=start");
            } else if (Code == IWL_SCAN_COMPLETE_UMAC
                       || Code == IWL_SCAN_COMPLETE_LMAC
                       || Code == IWL_SCAN_ITERATION_COMPLETE_UMAC) {
                GotDone = 1;
                if (gIwlSsidOk) {
                    IwlLogStage("scan=ok");
                    return 1;
                }
                /* 完成通知后面可能还有信标，先把本轮队列收完 */
                if (StopAt < 0) {
                    StopAt = (int)i + 400;
                }
            }
        }
        IwlStallMs(1);
        if (StopAt >= 0 && (int)i >= StopAt) {
            break;
        }
    }
    {
        char Line[56];
        char Hex[12];
        int n = 0;
        UINT32 k;
        const char *P = "scan=fail n=";
        while (*P) {
            Line[n++] = *P++;
        }
        HalSerialFormatHex(Hex, (UINT32)gIwlScanCount, 4);
        Line[n++] = Hex[2];
        Line[n++] = Hex[3];
        Line[n++] = Hex[4];
        Line[n++] = Hex[5];
        Line[n++] = ' ';
        Line[n++] = GotAck ? 'A' : 'a';
        Line[n++] = GotDone ? 'D' : 'd';
        for (k = 0; k < Ncode && n < 50; k++) {
            Line[n++] = ' ';
            HalSerialFormatHex(Hex, Codes[k] & 0xffu, 2);
            Line[n++] = Hex[2];
            Line[n++] = Hex[3];
            if (Codes[k] & 0x100u) {
                Line[n++] = 'n';
            }
        }
        Line[n] = 0;
        IwlLogStage(Line);
    }
    {
        char Line[64];
        char Hex[12];
        char Want[9];
        int n = 0;
        const char *P = "scan=miss b=";
        UINTN Wlen = 0;

        while (gIwlSsid[Wlen] && Wlen < 8u) {
            Wlen++;
        }
        IwlScanCopyVis(Want, (const UINT8 *)gIwlSsid, Wlen);
        while (*P && n < 48) {
            Line[n++] = *P++;
        }
        HalSerialFormatHex(Hex, BeaconN & 0xffu, 2);
        Line[n++] = Hex[2];
        Line[n++] = Hex[3];
        Line[n++] = ' ';
        Line[n++] = 'f';
        Line[n++] = 'c';
        Line[n++] = '=';
        HalSerialFormatHex(Hex, FirstFc, 2);
        Line[n++] = Hex[2];
        Line[n++] = Hex[3];
        Line[n++] = ' ';
        Line[n++] = 'h';
        Line[n++] = '=';
        P = Heard[0] ? Heard : "-";
        while (*P && n < 40) {
            Line[n++] = *P++;
        }
        Line[n++] = ' ';
        Line[n++] = 'w';
        Line[n++] = '=';
        P = Want[0] ? Want : "-";
        while (*P && n < 56) {
            Line[n++] = *P++;
        }
        Line[n] = 0;
        IwlLogStage(Line);
    }
    return 0;
}
