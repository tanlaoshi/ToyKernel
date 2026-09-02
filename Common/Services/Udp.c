/*
 * Udp.c — 极简 UDP
 */
#include "Udp.h"
#include "Hal.h"

#define UDP_HDR_LEN 8
#define UDP_RX_QUEUE 8

typedef struct {
    UINT16 SrcPort;
    UINT16 DstPort;
    UINT16 Length;
    UINT16 Checksum;
} __attribute__((packed)) UDP_HDR;

static UINT16 gBindPort;
static UDP_DATAGRAM gRxQ[UDP_RX_QUEUE];
static int gRxHead;
static int gRxTail;
static int gRxCount;

void UdpInit(void) {
    gBindPort = 0;
    gRxHead = gRxTail = gRxCount = 0;
}

int UdpBind(UINT16 Port) {
    gBindPort = Port;
    gRxHead = gRxTail = gRxCount = 0;
    return 0;
}

UINT16 UdpBoundPort(void) {
    return gBindPort;
}

int UdpSend(UINT32 DstIp, UINT16 DstPort, const void *Data, UINTN Len) {
    UINT8 Buf[UDP_HDR_LEN + UDP_PAYLOAD_MAX];
    UDP_HDR *Hdr;
    UINT16 SrcPort;

    if (!HalNetReady() || Data == 0 || Len > UDP_PAYLOAD_MAX) {
        return -1;
    }
    SrcPort = gBindPort != 0 ? gBindPort : 40000;
    Hdr = (UDP_HDR *)Buf;
    Hdr->SrcPort = (UINT16)((SrcPort >> 8) | (SrcPort << 8));
    Hdr->DstPort = (UINT16)((DstPort >> 8) | (DstPort << 8));
    Hdr->Length = (UINT16)(((UDP_HDR_LEN + Len) >> 8) | ((UDP_HDR_LEN + Len) << 8));
    Hdr->Checksum = 0; /* IPv4 允许 UDP 校验和为 0 */
    {
        UINT8 *P = Buf + UDP_HDR_LEN;
        const UINT8 *S = (const UINT8 *)Data;
        UINTN i;
        for (i = 0; i < Len; i++) {
            P[i] = S[i];
        }
    }
    return HalNetSendIp(DstIp, HAL_IP_PROTO_UDP, Buf, UDP_HDR_LEN + Len);
}

int UdpRecv(UDP_DATAGRAM *Out) {
    if (!Out || gRxCount == 0) {
        return 0;
    }
    *Out = gRxQ[gRxHead];
    gRxHead = (gRxHead + 1) % UDP_RX_QUEUE;
    gRxCount--;
    return 1;
}

void UdpInput(UINT32 SrcIp, UINT32 DstIp, const UINT8 *Payload, UINTN Len) {
    const UDP_HDR *Hdr;
    UINT16 DstPort;
    UINT16 SrcPort;
    UINT16 UdpLen;
    UINTN DataLen;

    (void)DstIp;
    if (Len < UDP_HDR_LEN) {
        return;
    }
    Hdr = (const UDP_HDR *)Payload;
    DstPort = (UINT16)((Hdr->DstPort >> 8) | (Hdr->DstPort << 8));
    SrcPort = (UINT16)((Hdr->SrcPort >> 8) | (Hdr->SrcPort << 8));
    UdpLen = (UINT16)((Hdr->Length >> 8) | (Hdr->Length << 8));
    if (gBindPort != 0 && DstPort != gBindPort) {
        return;
    }
    if (UdpLen < UDP_HDR_LEN || UdpLen > Len) {
        return;
    }
    DataLen = UdpLen - UDP_HDR_LEN;
    if (DataLen > UDP_PAYLOAD_MAX) {
        DataLen = UDP_PAYLOAD_MAX;
    }
    if (gRxCount >= UDP_RX_QUEUE) {
        gRxHead = (gRxHead + 1) % UDP_RX_QUEUE;
        gRxCount--;
    }
    gRxQ[gRxTail].SrcIp = SrcIp;
    gRxQ[gRxTail].SrcPort = SrcPort;
    gRxQ[gRxTail].DstPort = DstPort;
    gRxQ[gRxTail].Len = (UINT16)DataLen;
    {
        UINTN i;
        for (i = 0; i < DataLen; i++) {
            gRxQ[gRxTail].Data[i] = Payload[UDP_HDR_LEN + i];
        }
    }
    gRxTail = (gRxTail + 1) % UDP_RX_QUEUE;
    gRxCount++;
}
