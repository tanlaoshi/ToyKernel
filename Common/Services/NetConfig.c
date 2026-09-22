/*
 * NetConfig.c — 静态地址配置（PR-N-nic-addr）
 */
#include "NetConfig.h"
#include "Hal.h"
#include "HalDevices.h"
#include "LwIp.h"

#define NET_CFG_IP_QEMU      0x0A00020FU  /* 10.0.2.15 */
#define NET_CFG_MASK_DEFAULT 0xFFFFFF00U  /* /24 */
#define NET_CFG_GW_QEMU      0x0A000202U  /* 10.0.2.2 */
#define NET_CFG_DNS_QEMU     0x0A000203U  /* 10.0.2.3 */
#define NET_CFG_IP_NUC       0xC0A81F81U  /* 192.168.31.129 */
#define NET_CFG_GW_NUC       0xC0A81F01U  /* 192.168.31.1 */

static int gReady;
static UINT32 gIp = NET_CFG_IP_QEMU;
static UINT32 gMask = NET_CFG_MASK_DEFAULT;
static UINT32 gGw;
static UINT32 gDns;

void NetConfigEnsure(void) {
    if (gReady) {
        /* Attach 晚于首次 Ensure、或 Hal 后端曾空写：把配置表推回驱动 */
        if (gIp != 0 && HalNetGetIpAddress() != gIp) {
            HalNetSetIpAddress(gIp);
        }
        return;
    }
    if (HalCpuIsHypervisor()) {
        gIp = NET_CFG_IP_QEMU;
        gGw = NET_CFG_GW_QEMU;
        gDns = NET_CFG_DNS_QEMU;
    } else {
        /* NUC / 真机 LAN 默认（PR-N-i219-static） */
        gIp = NET_CFG_IP_NUC;
        gGw = NET_CFG_GW_NUC;
        gDns = 0;
    }
    HalNetSetIpAddress(gIp);
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

void NetConfigAdopt(UINT32 Ip, UINT32 Mask, UINT32 Gw, UINT32 Dns) {
    NetConfigEnsure();
    gIp = Ip;
    gMask = Mask;
    gGw = Gw;
    gDns = Dns;
    HalNetSetIpAddress(Ip);
}
