/*
 * NetConfig.h — 可配 IPv4 / 网关 / DNS（PR-N-nic-addr）
 *
 * 默认：hypervisor=QEMU SLIRP；真机=192.168.31.129 / gw .1（可 set* 覆盖）。
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

/* DHCP 成功后写入配置表（不改 netif；仅 Hal IP + 记忆） */
void NetConfigAdopt(UINT32 Ip, UINT32 Mask, UINT32 Gw, UINT32 Dns);

#endif
