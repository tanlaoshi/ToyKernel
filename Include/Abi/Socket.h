/*
 * Socket.h — 用户态 socket 常量（简化 ABI；完整 POSIX sockaddr 不做）
 *
 * SYS_SOCKET / SYS_BIND / SYS_LISTEN / SYS_ACCEPT / SYS_CONNECT
 * SYS_WRITE / SYS_READ / SYS_CLOSE 在 socket fd 上即 send/recv/close
 *
 * 默认 make LWIP=1（可用 LWIP=0 关掉）；首次 socket() 会自动 LwIpInit。
 * PR-A-net-dns：type=TOY_NET_SOCK_RESOLVE 时 rdx 为 TOY_NET_DNS_QUERY*（不占新 syscall 号）。
 */
#ifndef SOCKET_H
#define SOCKET_H

#include "BootTypes.h"

#define AF_INET     2
#define SOCK_STREAM 1
#define INADDR_ANY  0

#define TOY_NET_SOCK_RESOLVE 0x100
#define TOY_NET_NAME_MAX     127

typedef struct {
    char Name[TOY_NET_NAME_MAX + 1];
    UINT32 Ip;
} TOY_NET_DNS_QUERY;

#endif
