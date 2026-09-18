/*
 * NetConfig.c — 静态地址配置（PR-N-nic-addr）
 */
#include "NetConfig.h"
#include "Hal.h"
#include "HalDevices.h"
#include "LwIp.h"

#define NET_CFG_IP_DEFAULT   0x0A00020FU  /* 10.0.2.15 */
#define NET_CFG_MASK_DEFAULT 0xFFFFFF00U  /* /24 */
#define NET_CFG_GW_QEMU      0x0A000202U  /* 10.0.2.2 */
#define NET_CFG_DNS_QEMU     0x0A000203U  /* 10.0.2.3 */

static int gReady;
static UINT32 gIp = NET_CFG_IP_DEFAULT;
static UINT32 gMask = NET_CFG_MASK_DEFAULT;
static UINT32 gGw;
static UINT32 gDns;

void NetConfigEnsure(void) {
    if (gReady) {
        return;
    }
    if (HalCpuIsHypervisor()) {
        gGw = NET_CFG_GW_QEMU;
        gDns = NET_CFG_DNS_QEMU;
    } else {
        gGw = 0;
        gDns = 0;
    }
    gReady = 1;
}

UINT32 NetConfigGetIp(void) {
    NetConfigEnsure();
    return gIp;
}

UINT32 NetConfigGetMask(void) {
    NetConfigEnsure();
    return gMask;
}

UINT32 NetConfigGetGw(void) {
    NetConfigEnsure();
    return gGw;
}

UINT32 NetConfigGetDns(void) {
    NetConfigEnsure();
    return gDns;
}

static int ApplyStack(void) {
    HalNetSetIpAddress(gIp);
    return LwIpApplyConfig();
}

int NetConfigSetIp(UINT32 Ip) {
    NetConfigEnsure();
    gIp = Ip;
    return ApplyStack();
}

int NetConfigSetMask(UINT32 Mask) {
    NetConfigEnsure();
    gMask = Mask;
    return ApplyStack();
}

int NetConfigSetGw(UINT32 Gw) {
    NetConfigEnsure();
    gGw = Gw;
    return ApplyStack();
}

int NetConfigSetDns(UINT32 Dns) {
    NetConfigEnsure();
    gDns = Dns;
    return ApplyStack();
}
