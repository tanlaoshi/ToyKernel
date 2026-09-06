/*
 * toy_ping.c — lwIP 单次 ICMP echo（NO_SYS raw API）
 */
#include "lwip/opt.h"

#if LWIP_RAW && LWIP_ICMP

#include "toy_ping.h"
#include "toy_ip.h"
#include "LwIp.h"
#include "Hal.h"
#include "lwip/raw.h"
#include "lwip/icmp.h"
#include "lwip/inet_chksum.h"
#include "lwip/prot/icmp.h"
#include "lwip/mem.h"
#include "lwip/pbuf.h"
#include "lwip/ip4.h"
#include "lwip/prot/ip4.h"

#define TOY_PING_ID        0x4F53
#define TOY_PING_DATA_SIZE 32

static volatile int gPingDone;
static u16_t gPingSeq;

static void PingPrepareEcho(struct icmp_echo_hdr *Echo, u16_t Len) {
    u16_t i;
    u16_t DataLen = Len - (u16_t)sizeof(struct icmp_echo_hdr);

    ICMPH_TYPE_SET(Echo, ICMP_ECHO);
    ICMPH_CODE_SET(Echo, 0);
    Echo->chksum = 0;
    Echo->id = lwip_htons(TOY_PING_ID);
    Echo->seqno = lwip_htons(++gPingSeq);
    for (i = 0; i < DataLen; i++) {
        ((u8_t *)Echo)[sizeof(struct icmp_echo_hdr) + i] = (u8_t)i;
    }
    Echo->chksum = inet_chksum(Echo, Len);
}

static u8_t PingRecv(void *Arg, struct raw_pcb *Pcb, struct pbuf *P, const ip_addr_t *Addr) {
    struct icmp_echo_hdr *Echo;
    u16_t HdrLen;

    (void)Arg;
    (void)Pcb;
    (void)Addr;
    if (P == NULL || P->tot_len < IP_HLEN + sizeof(struct icmp_echo_hdr)) {
        return 0;
    }
    HdrLen = IPH_HL_BYTES((struct ip_hdr *)P->payload);
    if (HdrLen < IP_HLEN || P->tot_len < HdrLen + sizeof(struct icmp_echo_hdr)) {
        return 0;
    }
    if (pbuf_remove_header(P, HdrLen) != 0) {
        return 0;
    }
    Echo = (struct icmp_echo_hdr *)P->payload;
    if (ICMPH_TYPE(Echo) == ICMP_ER &&
        Echo->id == lwip_htons(TOY_PING_ID) &&
        Echo->seqno == lwip_htons(gPingSeq)) {
        gPingDone = 1;
        pbuf_free(P);
        return 1;
    }
    pbuf_add_header(P, HdrLen);
    return 0;
}

int ToyPing(UINT32 DstIp, int TimeoutMs) {
    struct raw_pcb *Pcb;
    ip4_addr_t Target;
    struct pbuf *P;
    struct icmp_echo_hdr *Echo;
    u16_t PingSize;
    int Tries;

    if (!LwIpActive()) {
        return -1;
    }
    ToyHostIpToLwIp(DstIp, &Target);
    gPingDone = 0;
    Pcb = raw_new(IP_PROTO_ICMP);
    if (Pcb == NULL) {
        return -1;
    }
    raw_recv(Pcb, PingRecv, NULL);
    raw_bind(Pcb, IP_ANY_TYPE);

    PingSize = (u16_t)(sizeof(struct icmp_echo_hdr) + TOY_PING_DATA_SIZE);
    P = pbuf_alloc(PBUF_IP, PingSize, PBUF_RAM);
    if (P == NULL) {
        raw_remove(Pcb);
        return -1;
    }
    Echo = (struct icmp_echo_hdr *)P->payload;
    PingPrepareEcho(Echo, PingSize);
    if (raw_sendto(Pcb, P, ip_2_ip4(&Target)) != ERR_OK) {
        pbuf_free(P);
        raw_remove(Pcb);
        return -1;
    }
    pbuf_free(P);

    Tries = TimeoutMs > 0 ? TimeoutMs : 3000;
    while (Tries-- > 0 && !gPingDone) {
        LwIpService();
        HalCpuHalt();
    }
    raw_remove(Pcb);
    return gPingDone ? 0 : -1;
}

#else

int ToyPing(UINT32 DstIp, int TimeoutMs) {
    (void)DstIp;
    (void)TimeoutMs;
    return -1;
}

#endif
