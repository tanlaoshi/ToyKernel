/*
 * Socket.h — 用户态 socket 常量（简化 ABI，非完整 POSIX sockaddr）
 *
 * SYS_SOCKET / SYS_BIND / SYS_LISTEN / SYS_ACCEPT / SYS_CONNECT
 * SYS_WRITE / SYS_READ / SYS_CLOSE 在 socket fd 上即 send/recv/close
 *
 * 需 make LWIP=1；首次 socket() 会自动 LwIpInit。
 */
#ifndef SOCKET_H
#define SOCKET_H

#define AF_INET     2
#define SOCK_STREAM 1
#define INADDR_ANY  0

#endif
