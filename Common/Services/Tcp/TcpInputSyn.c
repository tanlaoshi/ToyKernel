/*
 * TcpInputSyn.c — LISTEN / SYN_SENT / SYN_RECEIVED（PR-F-tcp-1）
 */
#include "Tcp.h"
#include "TcpPrivate.h"
#include "Console.h"
#include "Debug.h"

void TcpInputListen(UINT32 SrcIp, UINT16 SrcPort, UINT16 DstPort,
                    UINT8 Flags, UINT32 Seq, UINT16 Window) {
    if (DstPort != gLocalPort || (Flags & TCP_FLAG_SYN) == 0) {
        return;
    }
    gPeerIp = SrcIp;
    gPeerPort = SrcPort;
    gRcvNxt = Seq + 1;
    gSndUna = gIss;
    gSndNxt = gIss;
    gSndBufLen = 0;
    gPeerWnd = Window;
    if (gPeerWnd == 0) {
        gPeerWnd = 1;
    }
    gState = TCP_SYN_RECEIVED;
    if (TcpSendSegment(TCP_FLAG_SYN | TCP_FLAG_ACK, 0, 0, gSndNxt, gRcvNxt) != 0) {
        ConsoleWrite("tcp: syn-ack failed\n");
        gState = TCP_LISTEN;
        return;
    }
    gSndNxt++;
}

void TcpInputSynSent(UINT32 SrcIp, UINT16 SrcPort, UINT8 Flags,
                     UINT32 Seq, UINT32 Ack, UINT16 Window) {
    if (SrcIp != gPeerIp || SrcPort != gPeerPort) {
        return;
    }
    if ((Flags & (TCP_FLAG_SYN | TCP_FLAG_ACK)) == (TCP_FLAG_SYN | TCP_FLAG_ACK)) {
        gRcvNxt = Seq + 1;
        gSndUna = Ack;
        gSndNxt = Ack;
        gSndBufLen = 0;
        gPeerWnd = Window;
        if (gPeerWnd == 0) {
            gPeerWnd = 1;
        }
        TcpSendSegment(TCP_FLAG_ACK, 0, 0, gSndNxt, gRcvNxt);
        gState = TCP_ESTABLISHED;
        DebugWrite("tcp: connected\n");
        TcpFlushSend();
    }
}

int TcpInputSynRcvd(UINT32 SrcIp, UINT16 SrcPort, UINT8 Flags,
                    UINT32 Ack, UINT16 Window) {
    if (SrcIp != gPeerIp || SrcPort != gPeerPort) {
        return 0;
    }
    if ((Flags & TCP_FLAG_ACK) == 0 || Ack != gSndNxt) {
        return 0;
    }
    gSndUna = Ack;
    gPeerWnd = Window;
    if (gPeerWnd == 0) {
        gPeerWnd = 1;
    }
    gState = TCP_ESTABLISHED;
    ConsoleNotify("tcp: client connected\n");
    DebugWrite("tcp: established (echo)\n");
    return 1;
}
