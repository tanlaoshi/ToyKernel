/*
 * ToyNet.h — 用户态 libToyNet / libnet（PR-L4）
 *
 * BSD 风格薄封装；ABI 简化（无 sockaddr，ip/port 直接传）。
 * 需内核 make LWIP=1；首次 socket() 会触发内核 LwIpInit。
 *
 * 破坏性变更升 TOY_NET_ABI_VERSION_MAJOR。
 */
#ifndef TOY_NET_H
#define TOY_NET_H

#include <sys/types.h>

#define TOY_NET_ABI_VERSION_MAJOR 1
#define TOY_NET_ABI_VERSION_MINOR 0
#define TOY_NET_ABI_VERSION_PATCH 0
#define TOY_NET_ABI_VERSION_STRING "1.0.0"

/* 与 Include/Socket.h 一致 */
#define AF_INET     2
#define SOCK_STREAM 1
#define INADDR_ANY  0

/* 主机序 IPv4：ToyNetIpv4(10,0,2,2) == 0x0A000202（同 NETDEMO） */
static inline unsigned ToyNetIpv4(unsigned A, unsigned B, unsigned C, unsigned D) {
    return ((A & 0xffu) << 24) | ((B & 0xffu) << 16) | ((C & 0xffu) << 8) | (D & 0xffu);
}

int socket(int domain, int type, int protocol);
int connect(int fd, unsigned ip, unsigned port);
int bind(int fd, unsigned ip, unsigned port);
int listen(int fd, int backlog);
int accept(int fd);
/* send/recv：socket fd 上即 write/read */
ssize_t send(int fd, const void *buf, size_t len, int flags);
ssize_t recv(int fd, void *buf, size_t len, int flags);

#endif
