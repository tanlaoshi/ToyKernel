/*
 * LwIpDhcp.c — DHCP 客户端（PR-N-nic-dhcp / PR-N-i219-dhcp）
 *
 * 有 offer → 写回 NetConfig；超时/失败 → 停 DHCP，保留原静态配置。
 * shell IF=0：禁 Halt；用循环预算（与 ToyPing 同尺）。
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
#include "lwip/ip4_addr.h"
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

/* PR-N-i219-dhcp：DISCOVER 前清静态，避免带着 .129 去要租约 */
static void ClearNetifAddr(struct netif *Netif) {
    ip4_addr_t Any;

    ip4_addr_set_any(&Any);
    netif_set_addr(Netif, &Any, &Any, &Any);
}

int LwIpDhcpStart(int TimeoutMs) {
    struct netif *Netif;
    err_t Err;
    UINT32 Ms;
    UINT32 Budget;
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
    ClearNetifAddr(Netif);
    Err = dhcp_start(Netif);
    if (Err != ERR_OK) {
        DebugWrite("lwip: dhcp_start fail\n");
        (void)LwIpApplyConfig();
        return -1;
    }
    gDhcpRunning = 1;
    Ms = TimeoutMs > 0 ? (UINT32)TimeoutMs : 8000u;
    /* 与 ToyPing 同尺：IF=0 下空转 Poll；过短会误报 no offer */
    Budget = Ms * 4000u;
    if (Budget < 2000000u) {
        Budget = 2000000u;
    }
    while (Budget-- > 0) {
        LwIpService();
        if (dhcp_supplied_address(Netif)) {
            AdoptFromNetif(Netif);
            HalNetFormatIp(NetConfigGetIp(), IpBuf, (int)sizeof(IpBuf));
            DebugWrite("lwip: dhcp ok ip=");
            DebugWrite(IpBuf);
            DebugWrite("\n");
            return 0;
        }
        HalCpuRelax();
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
