/*
 * toy_netif.c — virtio-net 以太网 netif（linkoutput → NetSendEthernet）
 */
#include "lwip/opt.h"
#include "lwip/def.h"
#include "lwip/mem.h"
#include "lwip/pbuf.h"
#include "lwip/netif.h"
#include "lwip/etharp.h"
#include "netif/ethernet.h"
#include "toy_ip.h"
#include "Net.h"

extern void *memcpy(void *Dst, const void *Src, UINTN Len);

static struct netif gToyNetif;

/* QEMU user-net 网关 10.0.2.2 的固定 MAC，免首包 ARP 竞态 */
static const UINT8 gQemuGwMac[6] = { 0x52, 0x55, 0x0a, 0x00, 0x02, 0x02 };

static err_t ToyNetifOutput(struct netif *Netif, struct pbuf *P) {
    struct pbuf *Q;
    UINT8 Frame[1518];
    UINTN Off = 0;

    (void)Netif;
    for (Q = P; Q != NULL; Q = Q->next) {
        if (Off + Q->len > sizeof(Frame)) {
            return ERR_BUF;
        }
        memcpy(Frame + Off, Q->payload, Q->len);
        Off += Q->len;
    }
    if (Off < 14) {
        return ERR_BUF;
    }
    return NetSendEthernet(Frame, Off) == 0 ? ERR_OK : ERR_IF;
}

static err_t ToyNetifInit(struct netif *Netif) {
    UINT8 Mac[6];

    NetGetMac(Mac);
    Netif->name[0] = 'e';
    Netif->name[1] = 'n';
    Netif->output = etharp_output;
    Netif->linkoutput = ToyNetifOutput;
    Netif->mtu = 1500;
    Netif->hwaddr_len = ETH_HWADDR_LEN;
    Netif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_LINK_UP;
    memcpy(Netif->hwaddr, Mac, ETH_HWADDR_LEN);
    return ERR_OK;
}

struct netif *ToyNetifGet(void) {
    return &gToyNetif;
}

int ToyNetifAdd(UINT32 Ip, UINT32 Mask, UINT32 Gw) {
    ip4_addr_t IpAddr;
    ip4_addr_t NetMask;
    ip4_addr_t GwAddr;

    ToyHostIpToLwIp(Ip, &IpAddr);
    ToyHostIpToLwIp(Mask, &NetMask);
    ToyHostIpToLwIp(Gw, &GwAddr);
    if (netif_add(&gToyNetif, &IpAddr, &NetMask, &GwAddr, NULL,
                  ToyNetifInit, ethernet_input) == NULL) {
        return -1;
    }
    netif_set_default(&gToyNetif);
    netif_set_up(&gToyNetif);
    {
        ip4_addr_t GwIp;
        struct eth_addr GwMac;

        ToyHostIpToLwIp(Gw, &GwIp);
        memcpy(GwMac.addr, gQemuGwMac, 6);
        (void)etharp_add_static_entry(&GwIp, &GwMac);
    }
    return 0;
}

void ToyNetifInput(const UINT8 *Frame, UINTN Len) {
    struct pbuf *P;

    if (Len < 14 || Len > 1518) {
        return;
    }
    P = pbuf_alloc(PBUF_RAW, (u16_t)Len, PBUF_POOL);
    if (P == NULL) {
        return;
    }
    if (pbuf_take(P, Frame, (u16_t)Len) != ERR_OK) {
        pbuf_free(P);
        return;
    }
    if (gToyNetif.input(P, &gToyNetif) != ERR_OK) {
        pbuf_free(P);
    }
}
