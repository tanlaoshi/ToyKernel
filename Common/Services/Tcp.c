/*
 * Tcp.c — 单连接 TCP：握手/回显 + 发送缓冲、对端窗口、超时重传
 *
 * 无慢启动/拥塞控制；RTO 以 ShellTask 轮询次数计（见 TCP_RTO_POLLS）。
 */
#include "Tcp.h"
#include "Hal.h"
#include "Debug.h"

#define TCP_HDR_LEN       20
#define TCP_FLAG_FIN      0x01
#define TCP_FLAG_SYN      0x02
#define TCP_FLAG_RST      0x04
#define TCP_FLAG_PSH      0x08
#define TCP_FLAG_ACK      0x10

#define TCP_SND_BUF       2048
#define TCP_MSS           512
#define TCP_ADV_WND       4096
#define TCP_RTO_POLLS     8000
#define TCP_MAX_RETRANS   5

typedef struct {
    UINT16 SrcPort;
    UINT16 DstPort;
    UINT32 Seq;
    UINT32 Ack;
    UINT8  DataOff;
    UINT8  Flags;
    UINT16 Window;
    UINT16 Checksum;
    UINT16 Urgent;
} __attribute__((packed)) TCP_HDR;

static TCP_STATE gState;
static UINT16 gLocalPort;
static UINT16 gPeerPort;
static UINT32 gPeerIp;
static UINT16 gIss;

static UINT32 gSndUna;
static UINT32 gSndNxt;
static UINT8  gSndBuf[TCP_SND_BUF];
static UINT32 gSndBufLen;
static UINT16 gPeerWnd;

static UINT32 gRcvNxt;

static UINT32 gPollTicks;
static UINT32 gRtoDeadline;
static UINT8  gRetransCount;

static UINT16 HostToNet16(UINT16 V) {
    return (UINT16)((V >> 8) | (V << 8));
}

static UINT32 HostToNet32(UINT32 V) {
    return ((V & 0xFF) << 24) | ((V & 0xFF00) << 8) |
           ((V >> 8) & 0xFF00) | (V >> 24);
}

static UINT16 NetToHost16(UINT16 V) {
    return HostToNet16(V);
}

static UINT32 NetToHost32(UINT32 V) {
    return HostToNet32(V);
}

static void BufCopy(void *Dst, const void *Src, UINTN Len) {
    UINT8 *D = (UINT8 *)Dst;
    const UINT8 *S = (const UINT8 *)Src;
    UINTN i;

    for (i = 0; i < Len; i++) {
        D[i] = S[i];
    }
}

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
    UINT8 Pseudo[12 + 1500];
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

static int TcpSendSegment(UINT8 Flags, const void *Data, UINTN Len, UINT32 Seq, UINT32 Ack) {
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
    Hdr->Window = HostToNet16(TCP_ADV_WND);
    Hdr->Checksum = 0;
    Hdr->Urgent = 0;
    for (i = 0; i < Len; i++) {
        Buf[TCP_HDR_LEN + i] = ((const UINT8 *)Data)[i];
    }
    Hdr->Checksum = TcpChecksum(HalNetGetIp(), gPeerIp, Buf, TCP_HDR_LEN + Len);
    return HalNetSendIp(gPeerIp, HAL_IP_PROTO_TCP, Buf, TCP_HDR_LEN + Len);
}

static void TcpArmRetrans(void) {
    gRtoDeadline = gPollTicks + TCP_RTO_POLLS;
}

static void TcpFlushSend(void) {
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

static int TcpQueueBytes(const void *Data, UINTN Len) {
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

static void TcpProcessAck(UINT32 Ack) {
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

static void TcpRetransmit(void) {
    UINTN Chunk;
    UINT32 Unacked;

    if (gState != TCP_ESTABLISHED || gSndNxt <= gSndUna) {
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

void TcpInit(void) {
    gState = TCP_CLOSED;
    gLocalPort = 0;
    gPeerPort = 0;
    gPeerIp = 0;
    gIss = 1000;
    gSndUna = 0;
    gSndNxt = 0;
    gSndBufLen = 0;
    gPeerWnd = TCP_ADV_WND;
    gRcvNxt = 0;
    gPollTicks = 0;
    gRtoDeadline = 0;
    gRetransCount = 0;
}

int TcpListen(UINT16 Port) {
    TcpInit();
    gLocalPort = Port;
    gState = TCP_LISTEN;
    DebugWrite("tcp: listen ");
    DebugHex32(Port);
    DebugWrite("\n");
    return 0;
}

int TcpConnect(UINT32 DstIp, UINT16 DstPort) {
    TcpInit();
    gLocalPort = (UINT16)(40000 + (gIss & 0xFF));
    gPeerIp = DstIp;
    gPeerPort = DstPort;
    gSndUna = gIss;
    gSndNxt = gIss;
    gState = TCP_SYN_SENT;
    return TcpSendSegment(TCP_FLAG_SYN, 0, 0, gSndNxt, 0);
}

int TcpSend(const void *Data, UINTN Len) {
    if (TcpQueueBytes(Data, Len) != 0) {
        return -1;
    }
    TcpFlushSend();
    return 0;
}

void TcpClose(void) {
    if (gState == TCP_ESTABLISHED) {
        TcpFlushSend();
        TcpSendSegment(TCP_FLAG_FIN | TCP_FLAG_ACK, 0, 0, gSndNxt, gRcvNxt);
        gSndNxt++;
    }
    gState = TCP_CLOSED;
}

TCP_STATE TcpGetState(void) {
    return gState;
}

UINT16 TcpLocalPort(void) {
    return gLocalPort;
}

UINT32 TcpPeerIp(void) {
    return gPeerIp;
}

UINT16 TcpPeerPort(void) {
    return gPeerPort;
}

void TcpGetWindowStats(UINT32 *SndUna, UINT32 *SndNxt, UINT32 *BufLen,
                       UINT16 *PeerWnd, UINT8 *Retrans) {
    if (SndUna) {
        *SndUna = gSndUna;
    }
    if (SndNxt) {
        *SndNxt = gSndNxt;
    }
    if (BufLen) {
        *BufLen = gSndBufLen;
    }
    if (PeerWnd) {
        *PeerWnd = gPeerWnd;
    }
    if (Retrans) {
        *Retrans = gRetransCount;
    }
}

void TcpInput(UINT32 SrcIp, UINT32 DstIp, const UINT8 *Payload, UINTN Len) {
    const TCP_HDR *Hdr;
    UINT16 DstPort;
    UINT16 SrcPort;
    UINT8 Flags;
    UINT32 Seq;
    UINT32 Ack;
    UINTN HdrLen;
    UINTN DataLen;
    const UINT8 *Data;

    (void)DstIp;
    if (Len < TCP_HDR_LEN) {
        return;
    }
    Hdr = (const TCP_HDR *)Payload;
    DstPort = NetToHost16(Hdr->DstPort);
    SrcPort = NetToHost16(Hdr->SrcPort);
    Flags = Hdr->Flags;
    Seq = NetToHost32(Hdr->Seq);
    Ack = NetToHost32(Hdr->Ack);
    HdrLen = (Hdr->DataOff >> 4) * 4;
    if (HdrLen < TCP_HDR_LEN || HdrLen > Len) {
        return;
    }
    Data = Payload + HdrLen;
    DataLen = Len - HdrLen;

    if (gState == TCP_LISTEN) {
        if (DstPort != gLocalPort || (Flags & TCP_FLAG_SYN) == 0) {
            return;
        }
        gPeerIp = SrcIp;
        gPeerPort = SrcPort;
        gRcvNxt = Seq + 1;
        gSndUna = gIss;
        gSndNxt = gIss;
        gSndBufLen = 0;
        gPeerWnd = NetToHost16(Hdr->Window);
        if (gPeerWnd == 0) {
            gPeerWnd = 1;
        }
        gState = TCP_SYN_RECEIVED;
        TcpSendSegment(TCP_FLAG_SYN | TCP_FLAG_ACK, 0, 0, gSndNxt, gRcvNxt);
        gSndNxt++;
        return;
    }

    if (gState == TCP_SYN_SENT) {
        if (SrcIp != gPeerIp || SrcPort != gPeerPort) {
            return;
        }
        if ((Flags & (TCP_FLAG_SYN | TCP_FLAG_ACK)) == (TCP_FLAG_SYN | TCP_FLAG_ACK)) {
            gRcvNxt = Seq + 1;
            gSndUna = Ack;
            gSndNxt = Ack;
            gSndBufLen = 0;
            gPeerWnd = NetToHost16(Hdr->Window);
            if (gPeerWnd == 0) {
                gPeerWnd = 1;
            }
            TcpSendSegment(TCP_FLAG_ACK, 0, 0, gSndNxt, gRcvNxt);
            gState = TCP_ESTABLISHED;
            DebugWrite("tcp: connected\n");
            TcpFlushSend();
        }
        return;
    }

    if (gState == TCP_SYN_RECEIVED) {
        if (SrcIp != gPeerIp || SrcPort != gPeerPort) {
            return;
        }
        if (Flags & TCP_FLAG_ACK) {
            gState = TCP_ESTABLISHED;
            DebugWrite("tcp: established (echo)\n");
        }
        return;
    }

    if (gState != TCP_ESTABLISHED) {
        return;
    }
    if (SrcIp != gPeerIp || SrcPort != gPeerPort || DstPort != gLocalPort) {
        return;
    }

    gPeerWnd = NetToHost16(Hdr->Window);
    if (gPeerWnd == 0) {
        gPeerWnd = 1;
    }

    if (Flags & TCP_FLAG_RST) {
        gState = TCP_CLOSED;
        DebugWrite("tcp: reset\n");
        return;
    }

    if (Flags & TCP_FLAG_ACK) {
        TcpProcessAck(Ack);
    }

    if (DataLen > 0) {
        if (Seq != gRcvNxt) {
            /* 乱序丢弃；仍回 ACK 提示期望序号 */
            TcpSendSegment(TCP_FLAG_ACK, 0, 0, gSndNxt, gRcvNxt);
            return;
        }
        if (TcpQueueBytes(Data, DataLen) != 0) {
            DebugWrite("tcp: snd buf full\n");
            TcpSendSegment(TCP_FLAG_RST | TCP_FLAG_ACK, 0, 0, gSndNxt, gRcvNxt);
            gState = TCP_CLOSED;
            return;
        }
        gRcvNxt = Seq + (UINT32)DataLen;
        TcpFlushSend();
    }

    if (Flags & TCP_FLAG_FIN) {
        gRcvNxt = Seq + 1;
        TcpSendSegment(TCP_FLAG_ACK | TCP_FLAG_FIN, 0, 0, gSndNxt, gRcvNxt);
        gSndNxt++;
        gState = TCP_CLOSED;
        DebugWrite("tcp: closed\n");
    }
}

void TcpPoll(void) {
    gPollTicks++;
    if (gState != TCP_ESTABLISHED) {
        return;
    }
    if (gSndUna < gSndNxt && gPollTicks >= gRtoDeadline) {
        TcpRetransmit();
    }
    TcpFlushSend();
}
