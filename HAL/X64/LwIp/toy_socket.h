/*
 * toy_socket.h — lwIP 持久 TCP socket（用户态 SYS_SOCKET 后端）
 */
#ifndef TOY_SOCKET_H
#define TOY_SOCKET_H

#include "BootTypes.h"

#define TOY_SOCK_MAX     8
#define TOY_SOCK_PENDING 2

/* >=0 sock id；-1 失败 */
int ToySocketCreate(void);
int ToySocketBind(int Sock, UINT32 Ip, UINT16 Port);
int ToySocketListen(int Sock, int Backlog);
/* >=0 新连接 sock id；-1 超时/失败 */
int ToySocketAccept(int Sock, int TimeoutMs);
int ToySocketConnect(int Sock, UINT32 DstIp, UINT16 DstPort, int TimeoutMs);
int ToySocketSend(int Sock, const void *Data, UINTN Len);
/* >0 字节；0 暂无数据；-1 错；-2 对端关闭且缓冲空 */
int ToySocketRecv(int Sock, void *Buf, UINTN Len, int TimeoutMs);
int ToySocketClose(int Sock);

#endif
