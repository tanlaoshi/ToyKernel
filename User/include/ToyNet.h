/*
 * ToyNet.h — 用户态 libToyNet / libnet（PR-L4 / PR-A-net-dns / 网络双轨 C1）
 *
 * 第 2 轨（ToyOS 便利）：ToyNetConnect / ToyNetBind — (fd, ip, port) **主机序**。
 * 第 1 轨（POSIX）：connect / bind + sockaddr → 见 <sys/socket.h>（后续添加）。
 *
 * 2.0.0：原 connect/bind(fd,ip,port) 改名为 ToyNetConnect / ToyNetBind。
 * 破坏性变更升 TOY_NET_ABI_VERSION_MAJOR。
 */
#ifndef TOY_NET_H
#define TOY_NET_H

#include <sys/types.h>

#define TOY_NET_ABI_VERSION_MAJOR 2
#define TOY_NET_ABI_VERSION_MINOR 0
#define TOY_NET_ABI_VERSION_PATCH 0
#define TOY_NET_ABI_VERSION_STRING "2.0.0"

/* 与 Include/Socket.h 一致 */
#define AF_INET     2
#define SOCK_STREAM 1
#define INADDR_ANY  0

/* SYS_SOCKET 的 type：域名查询；应用请用 ToyNetResolve，不要直接 socket 此类型 */
#define TOY_NET_SOCK_RESOLVE 0x100
#define TOY_NET_NAME_MAX     127

typedef struct {
    char Name[TOY_NET_NAME_MAX + 1];
    unsigned Ip;
} ToyNetDnsQuery;

/*
 * ToySockAddrIn — 主机序地址（与 ToyNetConnect(fd, ip, port) 同一约定）。
 * 不是 POSIX 网络序 sockaddr_in。
 */
typedef struct {
    unsigned short Family;
    unsigned short Port;
    unsigned Addr;
} ToySockAddrIn;

/* 主机序 IPv4：ToyNetIpv4(10,0,2,2) == 0x0A000202（同 NETDEMO） */
static inline unsigned ToyNetIpv4(unsigned A, unsigned B, unsigned C, unsigned D) {
    return ((A & 0xffu) << 24) | ((B & 0xffu) << 16) | ((C & 0xffu) << 8) | (D & 0xffu);
}

int socket(int domain, int type, int protocol);
/* 主机序便利版（第 2 轨）；POSIX connect/bind 见后续 sys/socket.h */
int ToyNetConnect(int fd, unsigned ip, unsigned short port);
int ToyNetBind(int fd, unsigned ip, unsigned short port);
int listen(int fd, int backlog);
int accept(int fd);
/* send/recv：socket fd 上即 write/read */
ssize_t send(int fd, const void *buf, size_t len, int flags);
ssize_t recv(int fd, void *buf, size_t len, int flags);

void ToyNetAddrIn(ToySockAddrIn *Sa, unsigned Ip, unsigned Port);
int ToyNetConnectIn(int Fd, const ToySockAddrIn *Sa);
int ToyNetBindIn(int Fd, const ToySockAddrIn *Sa);
int ToyNetGetAddrIn(ToySockAddrIn *Sa, const char *Name, unsigned Port);
/* 点分 IPv4 / localhost 本地解析；其它名字走内核 lwIP DNS（Guest 需网卡；socket 会 LwIpInit） */
int ToyNetResolve(const char *Name, unsigned *OutIp);

#endif
