/*
 * IwlEapolIo.c — WPA2-PSK 四次握手辅助（PR-S-iwl-split-1，自 IwlEapol.c 搬家）
 */
#include "IwlPrivate.h"
#include "HalSerial.h"

int gIwlTxRsp;

int gIwlRxCode;

void IwlLogTxRsp(const IWL_RX_PKT *Pkt, UINTN Len) {
    char Line[40];
    char Hex[12];
    int n = 0;
    const char *P = "txa=l=";
    UINT8 Count = 0;

    if (gIwlTxRsp || !Pkt || Pkt->Hdr.Code != IWL_CMD_TX) {
        return;
    }
    if (Len >= sizeof(IWL_CMD_HDR) + 1u) {
        Count = Pkt->Data[0];
    }
    gIwlTxRsp = 1;
    while (*P) {
        Line[n++] = *P++;
    }
    HalSerialFormatHex(Hex, (UINT32)Len & 0xffu, 2);
    Line[n++] = Hex[2];
    Line[n++] = Hex[3];
    Line[n++] = ' ';
    Line[n++] = 'c';
    Line[n++] = '=';
    HalSerialFormatHex(Hex, Count, 2);
    Line[n++] = Hex[2];
    Line[n++] = Hex[3];
    if (Len >= sizeof(IWL_CMD_HDR) + 4u) {
        Line[n++] = ' ';
        Line[n++] = 'f';
        Line[n++] = '=';
        HalSerialFormatHex(Hex, Pkt->Data[3], 2);
        Line[n++] = Hex[2];
        Line[n++] = Hex[3];
    }
    if (Len >= sizeof(IWL_CMD_HDR) + 38u) {
        UINT16 St = (UINT16)Pkt->Data[36] | ((UINT16)Pkt->Data[37] << 8);
        Line[n++] = ' ';
        Line[n++] = 's';
        Line[n++] = '=';
        HalSerialFormatHex(Hex, St, 4);
        Line[n++] = Hex[2];
        Line[n++] = Hex[3];
        Line[n++] = Hex[4];
        Line[n++] = Hex[5];
    }
    Line[n] = 0;
    IwlLogStage(Line);
}

void IwlLogRxCode(const IWL_RX_PKT *Pkt, UINTN Len) {
    char Line[36];
    char Hex[12];
    int n = 0;
    const char *P = "rxc=";
    UINTN Pay;
    UINTN i;

    if (gIwlRxCode || !Pkt) {
        return;
    }
    gIwlRxCode = 1;
    while (*P) {
        Line[n++] = *P++;
    }
    HalSerialFormatHex(Hex, Pkt->Hdr.Code, 2);
    Line[n++] = Hex[2];
    Line[n++] = Hex[3];
    Line[n++] = ' ';
    Line[n++] = 'l';
    Line[n++] = '=';
    HalSerialFormatHex(Hex, (UINT32)Len & 0xffu, 2);
    Line[n++] = Hex[2];
    Line[n++] = Hex[3];
    if (Pkt->Hdr.Code == 0xf7u) {
        Pay = 0;
        if (Len > sizeof(IWL_CMD_HDR)) {
            Pay = Len - sizeof(IWL_CMD_HDR);
        }
        if (Pay > 8u) {
            Pay = 8u;
        }
        Line[n++] = ' ';
        Line[n++] = 'd';
        Line[n++] = '=';
        for (i = 0; i < Pay; i++) {
            HalSerialFormatHex(Hex, Pkt->Data[i], 2);
            Line[n++] = Hex[2];
            Line[n++] = Hex[3];
        }
    }
    Line[n] = 0;
    IwlLogStage(Line);
}

/*
 * 抽干 Hold+live RX；找到 EAPOL 返回 1。
 * 刀 #175：顺带累计 n=MPDU d=data u=单播-to-us b=beacon（可为 NULL）。
 * 旧版只 IwlRxTake，M2 的 TX 回执同步期间进 Hold 的 M3 会被漏掉 → m3to e=00。
 */
int IwlRxDrainForEapol(UINT8 *EapOut, UINTN *EapLenOut, UINTN Cap,
                              UINT32 *MpduN, UINT32 *DataN, UINT32 *UniN,
                              UINT32 *BeaconN) {
    IWL_RX_PKT *Pkt;
    UINTN Len;
    int Took = 0;

    while (Took < 32) {
        UINT8 Mutable[512];
        UINTN FLen;
        UINT8 *Ep;
        UINTN EpLen;
        UINTN k;
        UINT16 Fc;

        if (IwlRxTakeHeld(&Pkt, &Len)) {
        } else if (!IwlRxTake(&Pkt, &Len)) {
            break;
        }
        Took++;
        if (Pkt->Hdr.Code == IWL_CMD_TX) {
            IwlLogTxRsp(Pkt, Len);
            continue;
        }
        if (Pkt->Hdr.Code != IWL_RX_MPDU_CMD) {
            IwlLogRxCode(Pkt, Len);
            continue;
        }
        if (Len < 8) {
            continue;
        }
        if (MpduN) {
            (*MpduN)++;
        }
        FLen = Len - sizeof(IWL_CMD_HDR) - 4;
        if (FLen > sizeof(Mutable)) {
            FLen = sizeof(Mutable);
        }
        for (k = 0; k < FLen; k++) {
            Mutable[k] = Pkt->Data[4 + k];
        }
        if (FLen >= 24u) {
            Fc = (UINT16)Mutable[0] | ((UINT16)Mutable[1] << 8);
            if (((Fc >> 2) & 0x3u) == 0u) {
                UINT8 Sub = (UINT8)((Fc >> 4) & 0x0fu);
                if (BeaconN && (Sub == 8u || Sub == 5u)) {
                    (*BeaconN)++;
                }
            } else if (((Fc >> 2) & 0x3u) == 0x2u) {
                if (DataN) {
                    (*DataN)++;
                }
                if (UniN && IwlAddr1IsUs(Mutable)) {
                    (*UniN)++;
                }
            }
        }
        if (IwlFindEapol(Mutable, FLen, &Ep, &EpLen) && EpLen >= 99
            && EpLen <= Cap) {
            IwlEapolCopyN(EapOut, Ep, EpLen);
            *EapLenOut = EpLen;
            return 1;
        }
    }
    return 0;
}

int IwlSendEapol(const UINT8 *Eapol, UINTN EapolLen) {
    UINT8 Frame[320];
    UINTN i;
    UINTN Flen;

    if (EapolLen > 280) {
        return -1;
    }
    /* 非 QoS 数据。关联后 SendFrameRaw 放到 q5+AP_STA，速率 6M。 */
    Frame[0] = 0x08;
    Frame[1] = 0x01;
    Frame[2] = 0;
    Frame[3] = 0;
    IwlEapolCopyN(Frame + 4, gIwlBssid, 6);
    IwlEapolCopyN(Frame + 10, gIwlMac, 6);
    IwlEapolCopyN(Frame + 16, gIwlBssid, 6);
    Frame[22] = 0;
    Frame[23] = 0;
    Frame[24] = 0xAA;
    Frame[25] = 0xAA;
    Frame[26] = 0x03;
    Frame[27] = 0;
    Frame[28] = 0;
    Frame[29] = 0;
    Frame[30] = 0x88;
    Frame[31] = 0x8E;
    for (i = 0; i < EapolLen; i++) {
        Frame[32 + i] = Eapol[i];
    }
    Flen = 32 + EapolLen;
    return IwlSendFrameRaw(Frame, Flen);
}
