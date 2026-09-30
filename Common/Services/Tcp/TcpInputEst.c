/*
 * TcpInputEst.c — ESTABLISHED 入站（数据 / RST / FIN）（PR-F-tcp-1）
 */
#include "Tcp.h"
#include "TcpPrivate.h"
#include "Console.h"
#include "Debug.h"

/* 返回 1 继续处理 FIN；0 提前结束整段（乱序空洞 / 截断 / echo 失败）。 */
static int TcpInputData(UINT32 Seq, const UINT8 *Data, UINTN DataLen) {
    if (DataLen == 0) {
        return 1;
    }
    if (gClientMode && Seq != gRcvNxt && gRcvTotal == 0) {
        /* 仅在尚未接受过任何字节时对齐首段；勿用 gRcvBufLen==0（TcpRecv 排空后会误跳空洞） */
        gRcvNxt = Seq;
    }
    if (Seq != gRcvNxt) {
        if (Seq + (UINT32)DataLen <= gRcvNxt) {
            TcpSendSegment(TCP_FLAG_ACK, 0, 0, gSndNxt, gRcvNxt);
        } else {
            TcpSendSegment(TCP_FLAG_ACK, 0, 0, gSndNxt, gRcvNxt);
            return 0;
        }
    } else if (gClientMode) {
        UINT32 Space = TCP_RCV_BUF - gRcvBufLen;
        UINT32 Copy = (UINT32)DataLen;
        UINT32 i;

        if (Space == 0) {
            TcpSendSegment(TCP_FLAG_ACK, 0, 0, gSndNxt, gRcvNxt);
        } else {
            if (Copy > Space) {
                Copy = Space;
            }
            for (i = 0; i < Copy; i++) {
                gRcvBuf[gRcvBufLen + i] = Data[i];
            }
            gRcvBufLen += Copy;
            gRcvTotal += Copy;
            gRcvNxt = Seq + Copy;
            TcpSendSegment(TCP_FLAG_ACK, 0, 0, gSndNxt, gRcvNxt);
            if (Copy < (UINT32)DataLen) {
                return 0;
            }
        }
    } else {
        gRcvNxt = Seq + (UINT32)DataLen;
        /* 回显：直接发送（不经 snd_buf，避免与 snd_una 序号错位） */
        if (TcpSendSegment(TCP_FLAG_ACK | TCP_FLAG_PSH, Data, DataLen,
                           gSndNxt, gRcvNxt) != 0) {
            ConsoleWrite("tcp: echo send failed\n");
            DebugWrite("tcp: echo send failed\n");
            return 0;
        }
        gSndNxt += (UINT32)DataLen;
        if (gSndUna < gSndNxt) {
            TcpArmRetrans();
        }
        {
            char Buf[96];
            int Pos = 0;
            UINTN i;
            const char *Prefix = "tcp echo: ";

            while (Prefix[Pos] && Pos < (int)sizeof(Buf) - 2) {
                Buf[Pos] = Prefix[Pos];
                Pos++;
            }
            for (i = 0; i < DataLen && Pos < (int)sizeof(Buf) - 2; i++) {
                char C = (char)((const UINT8 *)Data)[i];
                if (C >= 32 && C <= 126) {
                    Buf[Pos++] = C;
                }
            }
            Buf[Pos++] = '\n';
            Buf[Pos] = 0;
            ConsoleNotify(Buf);
        }
    }
    return 1;
}

static void TcpInputFin(UINT8 Flags, UINT32 Seq, UINTN DataLen) {
    if ((Flags & TCP_FLAG_FIN) == 0) {
        return;
    }
    /*
     * FIN 序号必须紧接已收数据。过早 FIN（乱序）不得推进 gRcvNxt，
     * 否则后续段会被当成“对得上”而跳过 HTTP 头（S2：got=0 / head=.ELF）。
     */
    if (Seq + (UINT32)DataLen == gRcvNxt) {
        gRcvNxt++;
        gPeerClosed = 1;
        TcpSendSegment(TCP_FLAG_ACK | (gClientMode ? 0 : TCP_FLAG_FIN), 0, 0,
                       gSndNxt, gRcvNxt);
        if (!gClientMode) {
            gSndNxt++;
            gState = TCP_CLOSED;
            DebugWrite("tcp: closed\n");
        }
    } else {
        TcpSendSegment(TCP_FLAG_ACK, 0, 0, gSndNxt, gRcvNxt);
    }
}

void TcpInputEstablished(UINT16 DstPort, UINT32 SrcIp, UINT16 SrcPort,
                         UINT8 Flags, UINT32 Seq, UINT32 Ack, UINT16 Window,
                         const UINT8 *Data, UINTN DataLen) {
    if (SrcIp != gPeerIp || SrcPort != gPeerPort || DstPort != gLocalPort) {
        return;
    }

    gPeerWnd = Window;
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

    if (!TcpInputData(Seq, Data, DataLen)) {
        return;
    }
    TcpInputFin(Flags, Seq, DataLen);
}
