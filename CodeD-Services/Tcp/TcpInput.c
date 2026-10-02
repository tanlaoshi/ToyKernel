/*
 * TcpInput.c — 入站段编排（PR-F-tcp-1）
 * 握手：TcpInputSyn.c；已建立：TcpInputEst.c
 */
#include "Tcp.h"
#include "TcpPrivate.h"

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
    UINT16 Window;

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
    Window = NetToHost16(Hdr->Window);

    if (gState == TCP_LISTEN) {
        TcpInputListen(SrcIp, SrcPort, DstPort, Flags, Seq, Window);
        return;
    }

    if (gState == TCP_SYN_SENT) {
        TcpInputSynSent(SrcIp, SrcPort, Flags, Seq, Ack, Window);
        return;
    }

    if (gState == TCP_SYN_RECEIVED) {
        if (!TcpInputSynRcvd(SrcIp, SrcPort, Flags, Ack, Window)) {
            return;
        }
        /* 可能与本包同段的 PSH 数据：落到下方 ESTABLISHED 处理 */
    } else if (gState != TCP_ESTABLISHED) {
        return;
    }

    TcpInputEstablished(DstPort, SrcIp, SrcPort, Flags, Seq, Ack, Window, Data, DataLen);
}
