/*
 * IwlPoll.c — RX 轮询与链路查询（PR-S-iwl-split-2，自 Iwl.c 搬家）
 */
#include "IwlPrivate.h"
#include "HalSerial.h"
#include "Net.h"

void IwlLogDataTx(const IWL_RX_PKT *Pkt, UINTN Len) {
    char Line[16];
    char Hex[12];
    UINT16 St;
    int n = 0;
    const char *P = "dtx=";

    /* 握手的 M4 回执会先到。发现包发出之后的第一帧才算数。 */
    if (!gDatTxLogged || gDtxLogged || !Pkt || Len < sizeof(IWL_CMD_HDR) + 38u) {
        return;
    }
    gDtxLogged = 1;
    St = (UINT16)Pkt->Data[36] | ((UINT16)Pkt->Data[37] << 8);
    while (*P) {
        Line[n++] = *P++;
    }
    HalSerialFormatHex(Hex, St, 4);
    Line[n++] = Hex[2];
    Line[n++] = Hex[3];
    Line[n++] = Hex[4];
    Line[n++] = Hex[5];
    Line[n] = 0;
    IwlLogStage(Line);
}

void IwlPoll(void) {
    IWL_RX_PKT *Pkt;
    UINTN Len;

    if (!gIwlAlive) {
        return;
    }
    IwlRxPoll();
    {
    int Took = 0;
    while (Took < 24 && IwlRxTake(&Pkt, &Len)) {
        UINT8 Code;
        const UINT8 *Payload;
        UINTN PayLen;
        /* 曾栈上 2048：Worker 8K 栈上再套 lwIP 易紧；静态缓冲即可 */
        static UINT8 Mutable[2048];
        UINTN i;

        Took++;
        if (!Pkt || Len < sizeof(IWL_CMD_HDR)) {
            continue;
        }
        Code = Pkt->Hdr.Code;
        if (Code == IWL_CMD_TX) {
            IwlLogDataTx(Pkt, Len);
        }
        Payload = Pkt->Data;
        PayLen = Len - sizeof(IWL_CMD_HDR);
        if (Code == IWL_RX_MPDU_CMD && PayLen > 4) {
            UINT16 Bc;
            UINTN FLen = PayLen - 4;
            UINT32 St = 0;

            /*
             * 刀 #170：byte_count 是 802.11 帧长。DMA 长度还含帧后的状态字
             * 和对齐填充，CCMP 会把 MIC 对到填充上。
             */
            Bc = (UINT16)Payload[0] | ((UINT16)Payload[1] << 8);
            if (Bc >= 24u && (UINTN)Bc <= FLen) {
                FLen = Bc;
            }
            /* 刀 #179：帧后 4B = RX_MPDU_RES_STATUS（MIC_OK bit6 / DEC_DONE bit11） */
            if (4u + FLen + 4u <= PayLen) {
                St = (UINT32)Payload[4 + FLen]
                   | ((UINT32)Payload[4 + FLen + 1] << 8)
                   | ((UINT32)Payload[4 + FLen + 2] << 16)
                   | ((UINT32)Payload[4 + FLen + 3] << 24);
            }
            if (FLen > sizeof(Mutable)) {
                IwlLogStage("rx>buf");
                continue;
            }
            for (i = 0; i < FLen; i++) {
                Mutable[i] = Payload[4 + i];
            }
            if (gIwlWpa2Ok && !gRxStLogged && FLen >= 24u
                && (Mutable[1] & 0x40u) != 0) {
                char Line[20];
                char Hex[12];
                int n = 0;
                const char *P = "rx=st=";
                gRxStLogged = 1;
                while (*P) {
                    Line[n++] = *P++;
                }
                HalSerialFormatHex(Hex, (St >> 24) & 0xffu, 2);
                Line[n++] = Hex[2];
                Line[n++] = Hex[3];
                HalSerialFormatHex(Hex, (St >> 16) & 0xffu, 2);
                Line[n++] = Hex[2];
                Line[n++] = Hex[3];
                HalSerialFormatHex(Hex, (St >> 8) & 0xffu, 2);
                Line[n++] = Hex[2];
                Line[n++] = Hex[3];
                HalSerialFormatHex(Hex, St & 0xffu, 2);
                Line[n++] = Hex[2];
                Line[n++] = Hex[3];
                Line[n] = 0;
                IwlLogStage(Line);
            }
            if (gIwlWpa2Ok) {
                IwlRxDataToNet(Mutable, FLen, St);
            }
        }
        (void)Code;
    }
    }
}

int IwlGetLink(int *Up, UINT32 *Mbps, int *FullDuplex) {
    if (Up) {
        *Up = IwlAssociated() ? 1 : 0;
    }
    if (Mbps) {
        *Mbps = IwlAssociated() ? 54u : 0;
    }
    if (FullDuplex) {
        *FullDuplex = 1;
    }
    return 0;
}
