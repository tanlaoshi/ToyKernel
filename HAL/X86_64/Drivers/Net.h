/*
 * Net.h — virtio-net 驱动与 IPv4/ARP/ICMP，并为 UDP/TCP 提供发送入口
 */
#ifndef NET_H
#define NET_H

#include "BootConfig.h"

#define NET_IP_DEFAULT  0x0A00020FULL  /* 10.0.2.15 (QEMU user netdev) */
#define NET_IP_PROTO_ICMP 1
#define NET_IP_PROTO_TCP  6
#define NET_IP_PROTO_UDP  17

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
void NetGetStats(UINT32 *TxDone, UINT32 *RxFrames);

/* 发送 IPv4 载荷（已含上层协议头）；自动 ARP */
int NetSendIp(UINT32 DstIp, UINT8 Proto, const void *Payload, UINTN PayloadLen);
UINT16 NetChecksum(const void *Data, UINTN Len);

#endif
