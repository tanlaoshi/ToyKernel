/*
 * IwlTxCmdWait.c — IwlSendCmd 同步等回 / 超时诊断（PR-F-iwl-3）
 */
#include "IwlTxInternal.h"
#include "HalSerial.h"

/*
 * 抽一轮 RX：1 = ACK 成功；-1 = cmderr；0 = 本 ms 未决，继续等。
 */
int IwlSendCmdPollOnce(UINT32 Seq, UINT32 Opcode, UINT8 *LastCode,
                       UINT32 *RxHits) {
    IWL_RX_PKT *Pkt;
    UINTN RLen;
    int Took = 0;

    IwlRxPoll();
    /* 刀 #145：不封顶时 beacon 灌满，Wait 永不加，te=ok 后无黄字 */
    while (Took < 24 && IwlRxTake(&Pkt, &RLen)) {
        UINT8 Ridx = Pkt->Hdr.Idx;
        UINT8 Rqid = Pkt->Hdr.Qid;
        UINT8 Code = Pkt->Hdr.Code;
        const UINT8 *Pay;

        Took++;
        *LastCode = Code;
        (*RxHits)++;
        if (Code == 0x02u && RLen >= sizeof(IWL_CMD_HDR) + 4 + 8) {
            UINT16 BadSeq;
            Pay = Pkt->Data;
            BadSeq = (UINT16)Pay[6] | ((UINT16)Pay[7] << 8);
            if (Ridx == (UINT8)Seq || BadSeq == (UINT16)Seq) {
                char Line[56];
                char Hex[12];
                int N = 0;
                const char *P = "cmderr e=";
                UINT32 Et = (UINT32)Pay[0] | ((UINT32)Pay[1] << 8)
                          | ((UINT32)Pay[2] << 16) | ((UINT32)Pay[3] << 24);
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
                HalSerialFormatHex(Hex, Pay[4], 2);
                Line[N++] = Hex[2];
                Line[N++] = Hex[3];
                Line[N++] = ' ';
                Line[N++] = 'w';
                Line[N++] = '=';
                HalSerialFormatHex(Hex, Opcode, 2);
                Line[N++] = Hex[2];
                Line[N++] = Hex[3];
                Line[N] = 0;
                IwlLogStage(Line);
                IwlLogCmdq(Seq);
                gCmdRead = (gCmdRead + 1) & IWL_CMD_Q_MASK;
                return -1;
            }
        }
        /*
         * OpenBSD cmd_done：qid bit7=0 且 idx 对上即认。
         * REPLY_ERROR(0x02) 另计；其余一律成功（空 ACK 的 Code 即 opcode）。
         */
        if (!(Rqid & 0x80u) && Ridx == (UINT8)Seq) {
            if (Code == 0x02u) {
                continue;
            }
            gCmdRead = (gCmdRead + 1) & IWL_CMD_Q_MASK;
            return 1;
        }
        /* 刀 #98：同步等命令时勿丢 RX_MPDU（assoc 后 msg1 会来） */
        if (Code == IWL_RX_MPDU_CMD) {
            IwlRxHoldMpdu(Pkt, RLen);
            continue;
        }
        /* 刀 #45：host 回包（非 bit7）错序则暂存，供 scfg kick 认领 */
        if (!(Rqid & 0x80u) && Code != 0x02u) {
            gRspStash = 1;
            gRspStashCode = Code;
            gRspStashIdx = Ridx;
            gRspStashQid = Rqid;
        }
        if (*RxHits <= 3u) {
            char Line[40];
            char Hex[12];
            int N = 0;
            const char *P = "rxmiss c=";
            while (*P) {
                Line[N++] = *P++;
            }
            HalSerialFormatHex(Hex, Code, 2);
            Line[N++] = Hex[2];
            Line[N++] = Hex[3];
            Line[N++] = ' ';
            Line[N++] = 'i';
            Line[N++] = '=';
            HalSerialFormatHex(Hex, Ridx, 2);
            Line[N++] = Hex[2];
            Line[N++] = Hex[3];
            Line[N++] = ' ';
            Line[N++] = 'q';
            Line[N++] = '=';
            HalSerialFormatHex(Hex, Rqid, 2);
            Line[N++] = Hex[2];
            Line[N++] = Hex[3];
            Line[N++] = ' ';
            Line[N++] = 's';
            Line[N++] = '=';
            HalSerialFormatHex(Hex, Seq & 0xffu, 2);
            Line[N++] = Hex[2];
            Line[N++] = Hex[3];
            Line[N] = 0;
            IwlLogVerb(Line);
        }
    }
    return 0;
}

void IwlSendCmdLogTimeout(UINT32 Seq, UINT32 Opcode, UINT8 LastCode,
                          UINT32 RxHits) {
    char Line[48];
    char Hex[12];
    int N = 0;
    const char *P = "cmdto n=";

    while (*P) {
        Line[N++] = *P++;
    }
    HalSerialFormatHex(Hex, RxHits & 0xffu, 2);
    Line[N++] = Hex[2];
    Line[N++] = Hex[3];
    Line[N++] = ' ';
    Line[N++] = 'c';
    Line[N++] = '=';
    HalSerialFormatHex(Hex, LastCode, 2);
    Line[N++] = Hex[2];
    Line[N++] = Hex[3];
    Line[N++] = ' ';
    Line[N++] = 'o';
    Line[N++] = '=';
    HalSerialFormatHex(Hex, Opcode, 2);
    Line[N++] = Hex[2];
    Line[N++] = Hex[3];
    Line[N++] = ' ';
    Line[N++] = 'r';
    Line[N++] = '=';
    HalSerialFormatHex(Hex, IwlRxDiagClosed() & 0xffu, 2);
    Line[N++] = Hex[2];
    Line[N++] = Hex[3];
    Line[N++] = '/';
    HalSerialFormatHex(Hex, IwlRxDiagRead() & 0xffu, 2);
    Line[N++] = Hex[2];
    Line[N++] = Hex[3];
    Line[N] = 0;
    IwlLogStage(Line);
    IwlLogCmdq(Seq);
}
