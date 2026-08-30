/*
 * Net.h — virtio-net 驱动与简易 IPv4/ARP/ICMP
 */
#ifndef NET_H
#define NET_H

#include "BootConfig.h"

#define NET_IP_DEFAULT  0x0A00020FULL  /* 10.0.2.15 (QEMU user netdev) */

int  NetInit(void);
int  NetReady(void);
void NetPoll(void);

void NetGetMac(UINT8 Mac[6]);
UINT32 NetGetIp(void);
void NetSetIp(UINT32 Ip);

void NetFormatIp(UINT32 Ip, char *Buf, int BufLen);
int  NetParseIp(const char *Text, UINT32 *Ip);

void NetInfo(void);
int  NetPing(const char *Host, int TimeoutMs);

#endif
