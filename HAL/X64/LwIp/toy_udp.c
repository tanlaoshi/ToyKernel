/*
 * toy_udp.c — lwIP UDP bind / send / recv queue（NO_SYS raw API）
 */
#include "lwip/opt.h"

#if LWIP_UDP

#include "toy_udp.h"
#include "toy_ip.h"
#include "lwip/udp.h"
#include "lwip/pbuf.h"

extern void *memcpy(void *Dst, const void *Src, UINTN Len);

#define UDP_RX_QUEUE 8

static struct udp_pcb *gListenPcb;
static struct udp_pcb *gSendPcb;
static UINT16 gBindPort;
static UDP_DATAGRAM gRxQ[UDP_RX_QUEUE];
static int gRxHead;
static int gRxTail;
static int gRxCount;

static void UdpEnqueue(UINT32 SrcIp, UINT16 SrcPort, UINT16 DstPort,
                       const u8_t *Data, UINTN Len) {
    if (Len > UDP_PAYLOAD_MAX) {
        Len = UDP_PAYLOAD_MAX;
    }
    if (gRxCount >= UDP_RX_QUEUE) {
        gRxHead = (gRxHead + 1) % UDP_RX_QUEUE;
        gRxCount--;
    }
    gRxQ[gRxTail].SrcIp = SrcIp;
    gRxQ[gRxTail].SrcPort = SrcPort;
    gRxQ[gRxTail].DstPort = DstPort;
    gRxQ[gRxTail].Len = (UINT16)Len;
    memcpy(gRxQ[gRxTail].Data, Data, Len);
    gRxTail = (gRxTail + 1) % UDP_RX_QUEUE;
    gRxCount++;
}

static void UdpRecvCallback(void *Arg, struct udp_pcb *Pcb, struct pbuf *P,
                            const ip_addr_t *Addr, u16_t Port) {
    struct pbuf *Q;
    UINTN Off;
    UINT8 Buf[UDP_PAYLOAD_MAX];

    (void)Arg;
    (void)Pcb;
    if (P == NULL || Addr == NULL) {
        return;
    }
    Off = 0;
    for (Q = P; Q != NULL; Q = Q->next) {
        if (Off + Q->len > UDP_PAYLOAD_MAX) {
            break;
        }
        memcpy(Buf + Off, Q->payload, Q->len);
        Off += Q->len;
    }
    if (Off > 0) {
        UdpEnqueue(ToyLwIpToHost(ip_2_ip4(Addr)), Port, gBindPort, Buf, Off);
    }
    pbuf_free(P);
}

UINT16 LwIpUdpBoundPort(void) {
    return gBindPort;
}

int LwIpUdpBind(UINT16 Port) {
    if (gListenPcb != NULL) {
        udp_remove(gListenPcb);
        gListenPcb = NULL;
    }
    gListenPcb = udp_new_ip_type(IPADDR_TYPE_V4);
    if (gListenPcb == NULL) {
        return -1;
    }
    if (udp_bind(gListenPcb, IP_ANY_TYPE, Port) != ERR_OK) {
        udp_remove(gListenPcb);
        gListenPcb = NULL;
        return -1;
    }
    udp_recv(gListenPcb, UdpRecvCallback, NULL);
    gBindPort = Port;
    gRxHead = gRxTail = gRxCount = 0;
    return 0;
}

static struct udp_pcb *UdpGetSendPcb(void) {
    if (gListenPcb != NULL) {
        return gListenPcb;
    }
    if (gSendPcb == NULL) {
        gSendPcb = udp_new_ip_type(IPADDR_TYPE_V4);
        if (gSendPcb == NULL) {
            return NULL;
        }
        if (udp_bind(gSendPcb, IP_ANY_TYPE, 40000) != ERR_OK) {
            udp_remove(gSendPcb);
            gSendPcb = NULL;
            return NULL;
        }
    }
    return gSendPcb;
}

int LwIpUdpSend(UINT32 DstIp, UINT16 DstPort, const void *Data, UINTN Len) {
    struct udp_pcb *Pcb;
    struct pbuf *P;
    ip4_addr_t Dst;
    err_t Err;

    if (Data == NULL || Len == 0 || Len > UDP_PAYLOAD_MAX) {
        return -1;
    }
    Pcb = UdpGetSendPcb();
    if (Pcb == NULL) {
        return -1;
    }
    ToyHostIpToLwIp(DstIp, &Dst);
    P = pbuf_alloc(PBUF_TRANSPORT, (u16_t)Len, PBUF_RAM);
    if (P == NULL) {
        return -1;
    }
    memcpy(P->payload, Data, Len);
    Err = udp_sendto(Pcb, P, ip_2_ip4(&Dst), DstPort);
    pbuf_free(P);
    return Err == ERR_OK ? 0 : -1;
}

int LwIpUdpRecv(UDP_DATAGRAM *Out) {
    if (Out == NULL || gRxCount == 0) {
        return 0;
    }
    *Out = gRxQ[gRxHead];
    gRxHead = (gRxHead + 1) % UDP_RX_QUEUE;
    gRxCount--;
    return 1;
}

#else

UINT16 LwIpUdpBoundPort(void) {
    return 0;
}

int LwIpUdpBind(UINT16 Port) {
    (void)Port;
    return -1;
}

int LwIpUdpSend(UINT32 DstIp, UINT16 DstPort, const void *Data, UINTN Len) {
    (void)DstIp;
    (void)DstPort;
    (void)Data;
    (void)Len;
    return -1;
}

int LwIpUdpRecv(UDP_DATAGRAM *Out) {
    (void)Out;
    return 0;
}

#endif
