/*
 * LwIp.h — lwIP 门面（需 make LWIP=1 编译）
 */
#ifndef LWIP_SERVICE_H
#define LWIP_SERVICE_H

#include "BootTypes.h"
#include "Udp.h"

int  LwIpInit(void);
void LwIpPoll(void);
int  LwIpActive(void);
int  LwIpPing(UINT32 DstIp, int TimeoutMs);
int  LwIpTcpListen(UINT16 Port);
int  LwIpTcpListenStop(void);
UINT16 LwIpTcpListenPort(void);
int  LwIpUdpBind(UINT16 Port);
UINT16 LwIpUdpBoundPort(void);
int  LwIpUdpSend(UINT32 DstIp, UINT16 DstPort, const void *Data, UINTN Len);
int  LwIpUdpRecv(UDP_DATAGRAM *Out);
int  LwIpTcpConnectSend(UINT32 DstIp, UINT16 DstPort,
                        const void *Data, UINTN Len, int TimeoutMs);

/* 用户态 socket 后端（持久 TCP）；无 TOY_LWIP 时返回 -1 */
int  LwIpSocketCreate(void);
int  LwIpSocketConnect(int Sock, UINT32 DstIp, UINT16 DstPort);
int  LwIpSocketSend(int Sock, const void *Data, UINTN Len);
int  LwIpSocketRecv(int Sock, void *Buf, UINTN Len, int TimeoutMs);
int  LwIpSocketClose(int Sock);

#endif
