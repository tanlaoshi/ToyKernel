/*
 * Tcp.h — 极简单连接 TCP（listen/echo 或主动连接发送）
 */
#ifndef TCP_H
#define TCP_H

#include "BootConfig.h"

typedef enum {
    TCP_CLOSED = 0,
    TCP_LISTEN,
    TCP_SYN_SENT,
    TCP_SYN_RECEIVED,
    TCP_ESTABLISHED,
} TCP_STATE;

void TcpInit(void);
int  TcpListen(UINT16 Port);
int  TcpConnect(UINT32 DstIp, UINT16 DstPort);
int  TcpSend(const void *Data, UINTN Len);
void TcpClose(void);
TCP_STATE TcpGetState(void);
UINT16 TcpLocalPort(void);
UINT32 TcpPeerIp(void);
UINT16 TcpPeerPort(void);

void TcpInput(UINT32 SrcIp, UINT32 DstIp, const UINT8 *Payload, UINTN Len);
void TcpPoll(void);

#endif
