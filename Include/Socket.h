/*
 * Socket.h — 用户态 socket 常量（简化 ABI，非完整 POSIX sockaddr）
 *
 * SYS_SOCKET(domain, type, protocol) → fd
 * SYS_CONNECT(fd, ip_host_u32, port)
 * SYS_WRITE / SYS_READ 在 socket fd 上即 send/recv
 * SYS_CLOSE 关闭 pcb
 *
 * 需 make LWIP=1；首次 socket() 会自动 LwIpInit（等价 lwip on）。
 */
#ifndef SOCKET_H
#define SOCKET_H

#define AF_INET     2
#define SOCK_STREAM 1

#endif
