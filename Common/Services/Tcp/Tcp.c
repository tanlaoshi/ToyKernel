/*
 * Tcp.c — 自研单连接 TCP（legacy）：握手/回显 + 发送缓冲、对端窗口、超时重传
 *
 * 无慢启动/拥塞控制；RTO 以 ShellTask 轮询次数计（见 TCP_RTO_POLLS）。
 * 生产路径为 lwIP（ThirdParty/README.md）；本文件仅保留教学/无 lwIP 联调。
 */

#include "Tcp.h"
#include "TcpPrivate.h"
#include "Hal.h"
#include "Console.h"
#include "Debug.h"

TCP_STATE gState;
UINT16 gLocalPort;
UINT16 gPeerPort;
UINT32 gPeerIp;
UINT16 gIss;

UINT32 gSndUna;
UINT32 gSndNxt;
UINT8  gSndBuf[TCP_SND_BUF];
UINT32 gSndBufLen;
UINT16 gPeerWnd;

UINT32 gRcvNxt;

UINT32 gPollTicks;
UINT32 gRtoDeadline;
UINT8  gRetransCount;
int    gClientMode; /* PR-S2：主动连接收包，不回显 */
UINT8  gRcvBuf[TCP_RCV_BUF];
UINT32 gRcvBufLen;
UINT32 gRcvTotal; /* 本连接已接受字节数（排空缓冲后仍 >0） */
int    gPeerClosed;

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
    gClientMode = 0;
    gRcvBufLen = 0;
    gRcvTotal = 0;
    gPeerClosed = 0;
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

void TcpListenStop(void) {
    if (gState == TCP_LISTEN) {
        TcpInit();
    }
}

int TcpConnect(UINT32 DstIp, UINT16 DstPort) {
    TcpInit();
    gClientMode = 1;
    gLocalPort = (UINT16)(40000 + (gIss & 0xFF));
    gPeerIp = DstIp;
    gPeerPort = DstPort;
    gSndUna = gIss;
    gSndNxt = gIss;
    gState = TCP_SYN_SENT;
    if (TcpSendSegment(TCP_FLAG_SYN, 0, 0, gSndNxt, 0) != 0) {
        gState = TCP_CLOSED;
        return -1;
    }
    /* SYN 占一序号，便于超时重传 */
    gSndNxt = gIss + 1;
    TcpArmRetrans();
    return 0;
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

int TcpRecv(void *Buf, UINTN Max, UINTN *OutLen) {
    UINT32 N;
    UINT32 i;
    UINT8 *D = (UINT8 *)Buf;

    if (OutLen) {
        *OutLen = 0;
    }
    if (!Buf || Max == 0) {
        return -1;
    }
    N = gRcvBufLen;
    if (N > (UINT32)Max) {
        N = (UINT32)Max;
    }
    for (i = 0; i < N; i++) {
        D[i] = gRcvBuf[i];
    }
    if (N < gRcvBufLen) {
        for (i = 0; i < gRcvBufLen - N; i++) {
            gRcvBuf[i] = gRcvBuf[N + i];
        }
    }
    gRcvBufLen -= N;
    /* 排空后通告窗口，否则对端停在 win=0 */
    if (N > 0 && gClientMode && gState == TCP_ESTABLISHED) {
        TcpSendSegment(TCP_FLAG_ACK, 0, 0, gSndNxt, gRcvNxt);
    }
    if (OutLen) {
        *OutLen = N;
    }
    return 0;
}

int TcpPeerClosed(void) {
    return gPeerClosed;
}

int TcpSendAck(void) {
    if (gState != TCP_ESTABLISHED && !gPeerClosed) {
        return -1;
    }
    if (gState == TCP_CLOSED) {
        return -1;
    }
    return TcpSendSegment(TCP_FLAG_ACK, 0, 0, gSndNxt, gRcvNxt);
}

void TcpPoll(void) {
    gPollTicks++;
    if (gState == TCP_SYN_SENT) {
        if (gPollTicks >= gRtoDeadline) {
            if (gRetransCount >= TCP_MAX_RETRANS) {
                gState = TCP_CLOSED;
                return;
            }
            if (TcpSendSegment(TCP_FLAG_SYN, 0, 0, gIss, 0) == 0) {
                gRetransCount++;
                TcpArmRetrans();
            }
        }
        return;
    }
    if (gState != TCP_ESTABLISHED) {
        return;
    }
    if (gSndUna < gSndNxt && gPollTicks >= gRtoDeadline) {
        TcpRetransmit();
    }
    TcpFlushSend();
}
