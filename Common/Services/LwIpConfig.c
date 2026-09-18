/*
 * LwIpConfig.c — 从 NetConfig 绑 netif / DNS（PR-N-nic-addr）
 */
#include "LwIp.h"
#include "NetConfig.h"
#include "Hal.h"
#include "HalDevices.h"
#include "Debug.h"

#ifdef TOY_LWIP

#include "lwip/dns.h"
#include "lwip/ip_addr.h"
#include "toy_netif.h"
#include "toy_ip.h"

void LwIpConfigPushDns(void) {
    UINT32 DnsHost;
    ip_addr_t DnsServer;
    ip4_addr_t Dns4;

    DnsHost = NetConfigGetDns();
    if (DnsHost == 0) {
        return;
    }
    ToyHostIpToLwIp(DnsHost, &Dns4);
    ip_addr_copy_from_ip4(DnsServer, Dns4);
    dns_setserver(0, &DnsServer);
}

int LwIpConfigBindNetif(void) {
    return ToyNetifAdd(NetConfigGetIp(), NetConfigGetMask(), NetConfigGetGw());
}

int LwIpApplyConfig(void) {
    UINT64 IrqFlags;

    if (!LwIpActive()) {
        return 0;
    }
    IrqFlags = HalIrqSave();
    if (ToyNetifSetAddr(NetConfigGetIp(), NetConfigGetMask(),
                        NetConfigGetGw()) != 0) {
        HalIrqRestore(IrqFlags);
        return -1;
    }
    LwIpConfigPushDns();
    HalIrqRestore(IrqFlags);
    return 0;
}

void LwIpConfigLogDns(void) {
    char IpBuf[16];

    if (NetConfigGetDns() == 0) {
        DebugWrite("lwip: up dns=none\n");
        return;
    }
    HalNetFormatIp(NetConfigGetDns(), IpBuf, (int)sizeof(IpBuf));
    DebugWrite("lwip: up dns=");
    DebugWrite(IpBuf);
    DebugWrite("\n");
}

#else

int LwIpApplyConfig(void) {
    return 0;
}

#endif
