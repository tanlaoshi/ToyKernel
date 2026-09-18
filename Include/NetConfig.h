/*
 * NetConfig.h — 可配 IPv4 / 网关 / DNS（PR-N-nic-addr）
 *
 * 默认：hypervisor 用 QEMU SLIRP；真机 gw/dns=0（须 net config / set*）。
 */
#ifndef NET_CONFIG_H
#define NET_CONFIG_H

#include "BootTypes.h"

void NetConfigEnsure(void);
UINT32 NetConfigGetIp(void);
UINT32 NetConfigGetMask(void);
UINT32 NetConfigGetGw(void);
UINT32 NetConfigGetDns(void);

/* 成功 0；写入后同步 builtin IP，若 lwIP 已开则刷新 netif/DNS */
int NetConfigSetIp(UINT32 Ip);
int NetConfigSetMask(UINT32 Mask);
int NetConfigSetGw(UINT32 Gw);
int NetConfigSetDns(UINT32 Dns);

#endif
