/*
 * Tcp.c — 极简单连接 TCP：LISTEN 回显，或 CONNECT 后可发送
 *
 * 不做窗口探测/重传/拥塞控制；适合 QEMU user 网联调。
 */
#include "Tcp.h"
#include "Hal.h"
#include "Debug.h"

#define TCP_HDR_LEN     20
#define TCP_FLAG_FIN    0x01
#define TCP_FLAG_SYN    0x02
#define TCP_FLAG_RST    0x04
#define TCP_FLAG_PSH    0x08
#define TCP_FLAG_ACK    0x10

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
static UINT32 gSendSeq;
static UINT32 gRecvAck;
static UINT16 gIss;

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
    UINT8 Buf[TCP_HDR_LEN + 512];
    TCP_HDR *Hdr;
    UINTN i;

    if (Len > 512) {
        return -1;
    }
    Hdr = (TCP_HDR *)Buf;
    Hdr->SrcPort = HostToNet16(gLocalPort);
    Hdr->DstPort = HostToNet16(gPeerPort);
    Hdr->Seq = HostToNet32(Seq);
    Hdr->Ack = HostToNet32(Ack);
    Hdr->DataOff = (TCP_HDR_LEN / 4) << 4;
    Hdr->Flags = Flags;
    Hdr->Window = HostToNet16(4096);
    Hdr->Checksum = 0;
    Hdr->Urgent = 0;
    for (i = 0; i < Len; i++) {
        Buf[TCP_HDR_LEN + i] = ((const UINT8 *)Data)[i];
    }
    Hdr->Checksum = TcpChecksum(HalNetGetIp(), gPeerIp, Buf, TCP_HDR_LEN + Len);
    return HalNetSendIp(gPeerIp, HAL_IP_PROTO_TCP, Buf, TCP_HDR_LEN + Len);
}

void TcpInit(void) {
    gState = TCP_CLOSED;
    gLocalPort = 0;
    gPeerPort = 0;
    gPeerIp = 0;
    gSendSeq = 0;
    gRecvAck = 0;
    gIss = 1000;
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
    gLocalPort = 40000 + (gIss & 0xFF);
    gPeerIp = DstIp;
    gPeerPort = DstPort;
    gSendSeq = gIss;
    gState = TCP_SYN_SENT;
    return TcpSendSegment(TCP_FLAG_SYN, 0, 0, gSendSeq, 0);
}

int TcpSend(const void *Data, UINTN Len) {
    int Rc;
    if (gState != TCP_ESTABLISHED || Data == 0 || Len == 0) {
        return -1;
    }
    Rc = TcpSendSegment(TCP_FLAG_ACK | TCP_FLAG_PSH, Data, Len, gSendSeq, gRecvAck);
    if (Rc == 0) {
        gSendSeq += (UINT32)Len;
    }
    return Rc;
}

void TcpClose(void) {
    if (gState == TCP_ESTABLISHED) {
        TcpSendSegment(TCP_FLAG_FIN | TCP_FLAG_ACK, 0, 0, gSendSeq, gRecvAck);
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
        gRecvAck = Seq + 1;
        gSendSeq = gIss;
        gState = TCP_SYN_RECEIVED;
        TcpSendSegment(TCP_FLAG_SYN | TCP_FLAG_ACK, 0, 0, gSendSeq, gRecvAck);
        gSendSeq++;
        return;
    }

    if (gState == TCP_SYN_SENT) {
        if (SrcIp != gPeerIp || SrcPort != gPeerPort) {
            return;
        }
        if ((Flags & (TCP_FLAG_SYN | TCP_FLAG_ACK)) == (TCP_FLAG_SYN | TCP_FLAG_ACK)) {
            gRecvAck = Seq + 1;
            gSendSeq = Ack;
            TcpSendSegment(TCP_FLAG_ACK, 0, 0, gSendSeq, gRecvAck);
            gState = TCP_ESTABLISHED;
            DebugWrite("tcp: connected\n");
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
    if (Flags & TCP_FLAG_RST) {
        gState = TCP_CLOSED;
        DebugWrite("tcp: reset\n");
        return;
    }
    if (DataLen > 0) {
        gRecvAck = Seq + (UINT32)DataLen;
        /* echo */
        TcpSendSegment(TCP_FLAG_ACK | TCP_FLAG_PSH, Data, DataLen, gSendSeq, gRecvAck);
        gSendSeq += (UINT32)DataLen;
    } else if (Flags & TCP_FLAG_ACK) {
        /* keep-alive / pure ack */
    }
    if (Flags & TCP_FLAG_FIN) {
        gRecvAck = Seq + 1;
        TcpSendSegment(TCP_FLAG_ACK | TCP_FLAG_FIN, 0, 0, gSendSeq, gRecvAck);
        gState = TCP_CLOSED;
        DebugWrite("tcp: closed\n");
    }
}

void TcpPoll(void) {
    /* 预留：重传定时器 */
}
