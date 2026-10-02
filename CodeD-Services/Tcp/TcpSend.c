/*
 * TcpSend.c — 校验、发送缓冲、重传（PR-S-tcp-1）
 */
#include "Tcp.h"
#include "TcpPrivate.h"
#include "Hal.h"
#include "Console.h"
#include "Debug.h"

static void TcpBufConsume(UINT32 Bytes) {
    UINT32 i;

    if (Bytes >= gSndBufLen) {
        gSndBufLen = 0;
        return;
    }
    for (i = 0; i < gSndBufLen - Bytes; i++) {
        gSndBuf[i] = gSndBuf[Bytes + i];
    }
    gSndBufLen -= Bytes;
}

static UINT16 TcpChecksum(UINT32 SrcIp, UINT32 DstIp, const UINT8 *TcpSeg, UINTN TcpLen) {
    UINT8 Pseudo[12 + TCP_HDR_LEN + TCP_MSS];
    UINTN i;

    if (TcpLen + 12 > sizeof(Pseudo)) {
        return 0;
    }
    Pseudo[0] = (UINT8)(SrcIp >> 24);
    Pseudo[1] = (UINT8)(SrcIp >> 16);
    Pseudo[2] = (UINT8)(SrcIp >> 8);
    Pseudo[3] = (UINT8)SrcIp;
    Pseudo[4] = (UINT8)(DstIp >> 24);
    Pseudo[5] = (UINT8)(DstIp >> 16);
    Pseudo[6] = (UINT8)(DstIp >> 8);
    Pseudo[7] = (UINT8)DstIp;
    Pseudo[8] = 0;
    Pseudo[9] = HAL_IP_PROTO_TCP;
    Pseudo[10] = (UINT8)(TcpLen >> 8);
    Pseudo[11] = (UINT8)TcpLen;
    for (i = 0; i < TcpLen; i++) {
        Pseudo[12 + i] = TcpSeg[i];
    }
    return HostToNet16(HalNetChecksum(Pseudo, 12 + TcpLen));
}

static UINT16 TcpAdvertiseWindow(void) {
    UINT32 Space;

    if (!gClientMode) {
        return TCP_ADV_WND;
    }
    Space = TCP_RCV_BUF - gRcvBufLen;
    if (Space > TCP_CLIENT_ADV_MAX) {
        Space = TCP_CLIENT_ADV_MAX;
    }
    if (Space > 0xFFFF) {
        Space = 0xFFFF;
    }
    if (Space == 0) {
        return 0;
    }
    return (UINT16)Space;
}

int TcpSendSegment(UINT8 Flags, const void *Data, UINTN Len, UINT32 Seq, UINT32 Ack) {
    UINT8 Buf[TCP_HDR_LEN + TCP_MSS];
    TCP_HDR *Hdr;
    UINTN i;

    if (Len > TCP_MSS) {
        return -1;
    }
    Hdr = (TCP_HDR *)Buf;
    Hdr->SrcPort = HostToNet16(gLocalPort);
    Hdr->DstPort = HostToNet16(gPeerPort);
    Hdr->Seq = HostToNet32(Seq);
    Hdr->Ack = HostToNet32(Ack);
    Hdr->DataOff = (TCP_HDR_LEN / 4) << 4;
    Hdr->Flags = Flags;
    Hdr->Window = HostToNet16(TcpAdvertiseWindow());
    Hdr->Checksum = 0;
    Hdr->Urgent = 0;
    for (i = 0; i < Len; i++) {
        Buf[TCP_HDR_LEN + i] = ((const UINT8 *)Data)[i];
    }
    Hdr->Checksum = TcpChecksum(HalNetGetIpAddress(), gPeerIp, Buf, TCP_HDR_LEN + Len);
    return HalNetSendIp(gPeerIp, HAL_IP_PROTO_TCP, Buf, TCP_HDR_LEN + Len);
}

void TcpArmRetrans(void) {
    gRtoDeadline = gPollTicks + TCP_RTO_POLLS;
}

void TcpFlushSend(void) {
    UINT32 Flight;
    UINT32 WinLeft;

    while (gState == TCP_ESTABLISHED && gSndNxt < gSndUna + gSndBufLen) {
        Flight = gSndNxt - gSndUna;
        if (Flight >= gPeerWnd) {
            break;
        }
        WinLeft = gPeerWnd - Flight;
        {
            UINT32 Off = gSndNxt - gSndUna;
            UINT32 Unsent = gSndUna + gSndBufLen - gSndNxt;
            UINTN Chunk = TCP_MSS;

            if (Chunk > Unsent) {
                Chunk = Unsent;
            }
            if (Chunk > WinLeft) {
                Chunk = WinLeft;
            }
            if (Chunk == 0) {
                break;
            }
            if (TcpSendSegment(TCP_FLAG_ACK | TCP_FLAG_PSH, gSndBuf + Off, Chunk,
                               gSndNxt, gRcvNxt) != 0) {
                break;
            }
            gSndNxt += (UINT32)Chunk;
            if (gSndUna < gSndNxt) {
                TcpArmRetrans();
            }
        }
    }
}

int TcpQueueBytes(const void *Data, UINTN Len) {
    if (gState != TCP_ESTABLISHED || Data == 0 || Len == 0) {
        return -1;
    }
    if (gSndBufLen + Len > TCP_SND_BUF) {
        return -1;
    }
    BufCopy(gSndBuf + gSndBufLen, Data, Len);
    gSndBufLen += (UINT32)Len;
    return 0;
}

void TcpProcessAck(UINT32 Ack) {
    UINT32 Acked;

    if (Ack <= gSndUna) {
        return;
    }
    Acked = Ack - gSndUna;
    if (Acked > gSndBufLen) {
        Acked = gSndBufLen;
    }
    TcpBufConsume(Acked);
    gSndUna = Ack;
    if (gSndNxt < gSndUna) {
        gSndNxt = gSndUna;
    }
    if (gSndBufLen == 0) {
        gRetransCount = 0;
    } else if (gSndUna < gSndNxt) {
        TcpArmRetrans();
    }
    TcpFlushSend();
}

void TcpRetransmit(void) {
    UINTN Chunk;
    UINT32 Unacked;

    if (gState != TCP_ESTABLISHED || gSndNxt <= gSndUna) {
        return;
    }
    /* 仅重传 snd_buf 中由 TcpSend 入队的数据；回显为即时发送不在缓冲内 */
    if (gSndBufLen == 0) {
        gSndUna = gSndNxt;
        gRetransCount = 0;
        return;
    }
    Unacked = gSndNxt - gSndUna;
    Chunk = TCP_MSS;
    if (Chunk > Unacked) {
        Chunk = Unacked;
    }
    if (Chunk == 0) {
        return;
    }
    if (TcpSendSegment(TCP_FLAG_ACK | TCP_FLAG_PSH, gSndBuf, Chunk, gSndUna, gRcvNxt) != 0) {
        return;
    }
    gRetransCount++;
    if (gRetransCount >= TCP_MAX_RETRANS) {
        DebugWrite("tcp: retrans limit\n");
        TcpSendSegment(TCP_FLAG_RST | TCP_FLAG_ACK, 0, 0, gSndNxt, gRcvNxt);
        gState = TCP_CLOSED;
        return;
    }
    gRtoDeadline = gPollTicks + TCP_RTO_POLLS * (1U << (gRetransCount - 1));
}
