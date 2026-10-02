/*
 * socket.c — POSIX connect/bind + 字节序（刀 C）
 * 直接 toy_connect/toy_bind（主机序）；不链 libToyNet（CRT 须自洽）。
 * socket/listen/accept/send/recv 仍在 libToyNet。
 */
#include <errno.h>
#include <sys/socket.h>
#include <toyos/syscall.h>

uint16_t htons(uint16_t Host) {
    return (uint16_t)((Host << 8) | (Host >> 8));
}

uint16_t ntohs(uint16_t Net) {
    return htons(Net);
}

uint32_t htonl(uint32_t Host) {
    return ((Host & 0x000000ffu) << 24) |
           ((Host & 0x0000ff00u) << 8) |
           ((Host & 0x00ff0000u) >> 8) |
           ((Host & 0xff000000u) >> 24);
}

uint32_t ntohl(uint32_t Net) {
    return htonl(Net);
}

static void SockFail(long R) {
    int E;

    if (R >= 0) {
        return;
    }
    E = (int)(-R);
    if (E >= 1 && E < 256) {
        errno = E;
    } else {
        errno = EIO;
    }
}

static int SockAddrToHost(const struct sockaddr *Addr, socklen_t Len,
                          unsigned *OutIp, unsigned short *OutPort) {
    const struct sockaddr_in *In;

    if (!Addr || !OutIp || !OutPort) {
        errno = EINVAL;
        return -1;
    }
    if (Len < (socklen_t)sizeof(struct sockaddr_in)) {
        errno = EINVAL;
        return -1;
    }
    if (Addr->sa_family != AF_INET) {
        errno = EINVAL;
        return -1;
    }
    In = (const struct sockaddr_in *)(const void *)Addr;
    *OutIp = ntohl(In->sin_addr.s_addr);
    *OutPort = ntohs(In->sin_port);
    return 0;
}

int connect(int SockFd, const struct sockaddr *Addr, socklen_t AddrLen) {
    unsigned Ip;
    unsigned short Port;
    long R;

    if (SockFd < 0) {
        errno = EBADF;
        return -1;
    }
    if (SockAddrToHost(Addr, AddrLen, &Ip, &Port) != 0) {
        return -1;
    }
    R = toy_connect(SockFd, (long)Ip, (long)Port);
    if (R < 0) {
        SockFail(R);
        return -1;
    }
    return 0;
}

int bind(int SockFd, const struct sockaddr *Addr, socklen_t AddrLen) {
    unsigned Ip;
    unsigned short Port;
    long R;

    if (SockFd < 0) {
        errno = EBADF;
        return -1;
    }
    if (SockAddrToHost(Addr, AddrLen, &Ip, &Port) != 0) {
        return -1;
    }
    R = toy_bind(SockFd, (long)Ip, (long)Port);
    if (R < 0) {
        SockFail(R);
        return -1;
    }
    return 0;
}
