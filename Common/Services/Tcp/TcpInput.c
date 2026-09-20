/*
 * TcpInput.c — 入站段（PR-S-tcp-1）
 */
#include "Tcp.h"
#include "TcpPrivate.h"
#include "Hal.h"
#include "Console.h"
#include "Debug.h"

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
        if (TcpSendSegment(TCP_FLAG_SYN | TCP_FLAG_ACK, 0, 0, gSndNxt, gRcvNxt) != 0) {
            ConsoleWrite("tcp: syn-ack failed\n");
            gState = TCP_LISTEN;
            return;
        }
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
        if ((Flags & TCP_FLAG_ACK) == 0 || Ack != gSndNxt) {
            return;
        }
        gSndUna = Ack;
        gPeerWnd = NetToHost16(Hdr->Window);
        if (gPeerWnd == 0) {
            gPeerWnd = 1;
        }
        gState = TCP_ESTABLISHED;
        ConsoleNotify("tcp: client connected\n");
        DebugWrite("tcp: established (echo)\n");
        /* 可能与本包同段的 PSH 数据：落到下方 ESTABLISHED 处理 */
    } else if (gState != TCP_ESTABLISHED) {
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
        if (gClientMode && Seq != gRcvNxt && gRcvTotal == 0) {
            /* 仅在尚未接受过任何字节时对齐首段；勿用 gRcvBufLen==0（TcpRecv 排空后会误跳空洞） */
            gRcvNxt = Seq;
        }
        if (Seq != gRcvNxt) {
            if (Seq + (UINT32)DataLen <= gRcvNxt) {
                TcpSendSegment(TCP_FLAG_ACK, 0, 0, gSndNxt, gRcvNxt);
            } else {
                TcpSendSegment(TCP_FLAG_ACK, 0, 0, gSndNxt, gRcvNxt);
                return;
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
                    return;
                }
            }
        } else {
            gRcvNxt = Seq + (UINT32)DataLen;
            /* 回显：直接发送（不经 snd_buf，避免与 snd_una 序号错位） */
            if (TcpSendSegment(TCP_FLAG_ACK | TCP_FLAG_PSH, Data, DataLen,
                               gSndNxt, gRcvNxt) != 0) {
                ConsoleWrite("tcp: echo send failed\n");
                DebugWrite("tcp: echo send failed\n");
                return;
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
    }

    if (Flags & TCP_FLAG_FIN) {
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
}
