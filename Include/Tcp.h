/*
 * Tcp.h — 自研单连接 TCP（legacy）
 *
 * 默认/未 lwip on 时由 Shell 使用。make LWIP=1 且 lwip on 后走 lwIP，
 * 本模块停用。不再扩展多连接/拥塞控制；策略见 ThirdParty/README.md。
 */
#ifndef TCP_H
#define TCP_H

#include "BootTypes.h"

typedef enum {
    TCP_CLOSED = 0,
    TCP_LISTEN,
    TCP_SYN_SENT,
    TCP_SYN_RECEIVED,
    TCP_ESTABLISHED,
} TCP_STATE;

void TcpInit(void);
int  TcpListen(UINT16 Port);
void TcpListenStop(void);
int  TcpConnect(UINT32 DstIp, UINT16 DstPort);
int  TcpSend(const void *Data, UINTN Len);
void TcpClose(void);
TCP_STATE TcpGetState(void);
UINT16 TcpLocalPort(void);
UINT32 TcpPeerIp(void);
UINT16 TcpPeerPort(void);

void TcpGetWindowStats(UINT32 *SndUna, UINT32 *SndNxt, UINT32 *BufLen,
                       UINT16 *PeerWnd, UINT8 *Retrans);

void TcpInput(UINT32 SrcIp, UINT32 DstIp, const UINT8 *Payload, UINTN Len);
void TcpPoll(void);

#endif
