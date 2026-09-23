/*
 * sys/socket.h — 第 1 轨 POSIX 套接字声明（PR-U-abi-dual 刀 C）
 *
 * connect/bind：网络序 sockaddr → 内部 ToyNetConnect/Bind（主机序）。
 * socket/listen/accept/send/recv：实现仍在 libToyNet；accept 无对端地址出参。
 */
#ifndef SYS_SOCKET_H
#define SYS_SOCKET_H

#include <sys/types.h>
#include <stddef.h>

#define AF_INET     2
#define SOCK_STREAM 1
#define INADDR_ANY  0

struct sockaddr {
    sa_family_t sa_family;
    char        sa_data[14];
};

struct in_addr {
    uint32_t s_addr; /* 网络序 */
};

struct sockaddr_in {
    sa_family_t    sin_family;
    uint16_t       sin_port;   /* 网络序 */
    struct in_addr sin_addr;   /* 网络序 */
};

uint16_t htons(uint16_t Host);
uint16_t ntohs(uint16_t Net);
uint32_t htonl(uint32_t Host);
uint32_t ntohl(uint32_t Net);

int socket(int domain, int type, int protocol);
int bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
int connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
int listen(int fd, int backlog);
/* ToyOS：无 peer 出参（非完整 POSIX） */
int accept(int fd);
ssize_t send(int fd, const void *buf, size_t len, int flags);
ssize_t recv(int fd, void *buf, size_t len, int flags);

#endif
