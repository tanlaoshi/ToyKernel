/*
 * LwIpDhcp.c — DHCP 客户端（PR-N-nic-dhcp）
 *
 * 有 offer → 写回 NetConfig；超时/失败 → 停 DHCP，保留原静态配置。
 */
#include "LwIp.h"
#include "NetConfig.h"
#include "Hal.h"
#include "HalDevices.h"
#include "Debug.h"

#ifdef TOY_LWIP

#include "lwip/dhcp.h"
#include "lwip/dns.h"
#include "lwip/netif.h"
#include "toy_netif.h"
#include "toy_ip.h"

static int gDhcpRunning;

void LwIpDhcpStop(void) {
    struct netif *Netif;

    if (!gDhcpRunning) {
        return;
    }
    Netif = ToyNetifGet();
    if (Netif) {
        dhcp_release_and_stop(Netif);
    }
    gDhcpRunning = 0;
}

int LwIpDhcpRunning(void) {
    return gDhcpRunning;
}

static void AdoptFromNetif(struct netif *Netif) {
    UINT32 Ip;
    UINT32 Mask;
    UINT32 Gw;
    UINT32 Dns;
    const ip_addr_t *DnsSrv;

    Ip = ToyLwIpToHost(netif_ip4_addr(Netif));
    Mask = ToyLwIpToHost(netif_ip4_netmask(Netif));
    Gw = ToyLwIpToHost(netif_ip4_gw(Netif));
    Dns = 0;
    DnsSrv = dns_getserver(0);
    if (DnsSrv != 0 && !ip_addr_isany(DnsSrv)) {
        Dns = ToyLwIpToHost(ip_2_ip4(DnsSrv));
    }
    NetConfigAdopt(Ip, Mask, Gw, Dns);
}

int LwIpDhcpStart(int TimeoutMs) {
    struct netif *Netif;
    err_t Err;
    int Left;
    char IpBuf[16];

    if (!HalNetReady()) {
        return -1;
    }
    if (!LwIpActive() && LwIpInit() != 0) {
        return -1;
    }
    Netif = ToyNetifGet();
    if (!Netif) {
        return -1;
    }
    LwIpDhcpStop();
    Err = dhcp_start(Netif);
    if (Err != ERR_OK) {
        DebugWrite("lwip: dhcp_start fail\n");
        return -1;
    }
    gDhcpRunning = 1;
    Left = TimeoutMs > 0 ? TimeoutMs : 8000;
    while (Left-- > 0) {
        LwIpService();
        if (dhcp_supplied_address(Netif)) {
            AdoptFromNetif(Netif);
            HalNetFormatIp(NetConfigGetIp(), IpBuf, (int)sizeof(IpBuf));
            DebugWrite("lwip: dhcp ok ip=");
            DebugWrite(IpBuf);
            DebugWrite("\n");
            return 0;
        }
        HalCpuHalt();
    }
    DebugWrite("lwip: dhcp timeout (keep static)\n");
    LwIpDhcpStop();
    (void)LwIpApplyConfig();
    return -1;
}

#else

int LwIpDhcpStart(int TimeoutMs) {
    (void)TimeoutMs;
    return -1;
}

void LwIpDhcpStop(void) {
}

int LwIpDhcpRunning(void) {
    return 0;
}

#endif
