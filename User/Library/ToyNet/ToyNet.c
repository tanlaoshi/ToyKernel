/*
 * ToyNet.c — libToyNet（PR-L4 / PR-N-dns / PR-A-net-dns）
 */
#include <errno.h>
#include <string.h>
#include <ToyNet.h>
#include <unistd.h>
#include <toyos/syscall.h>

/* syscall 返回 -(errno) 时贯通；-1 仍映射 EIO */
static void ToyNetFail(long R) {
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

int socket(int domain, int type, int protocol) {
    long r = toy_socket(domain, type, protocol);
    if (r < 0) {
        ToyNetFail(r == -1 ? (long)-EIO : r);
        return -1;
    }
    return (int)r;
}

int ToyNetConnect(int fd, unsigned ip, unsigned short port) {
    long r;
    if (fd < 0) {
        errno = EBADF;
        return -1;
    }
    r = toy_connect(fd, (long)ip, (long)port);
    if (r < 0) {
        ToyNetFail(r);
        return -1;
    }
    return 0;
}

int ToyNetBind(int fd, unsigned ip, unsigned short port) {
    long r;
    if (fd < 0) {
        errno = EBADF;
        return -1;
    }
    r = toy_bind(fd, (long)ip, (long)port);
    if (r < 0) {
        ToyNetFail(r);
        return -1;
    }
    return 0;
}

int listen(int fd, int backlog) {
    long r;
    if (fd < 0) {
        errno = EBADF;
        return -1;
    }
    r = toy_listen(fd, backlog);
    if (r < 0) {
        ToyNetFail(r);
        return -1;
    }
    return 0;
}

int accept(int fd) {
    long r;
    if (fd < 0) {
        errno = EBADF;
        return -1;
    }
    r = toy_accept(fd);
    if (r < 0) {
        ToyNetFail(r);
        return -1;
    }
    return (int)r;
}

ssize_t send(int fd, const void *buf, size_t len, int flags) {
    (void)flags;
    return write(fd, buf, len);
}

ssize_t recv(int fd, void *buf, size_t len, int flags) {
    (void)flags;
    return read(fd, buf, len);
}

void ToyNetAddrIn(ToySockAddrIn *Sa, unsigned Ip, unsigned Port) {
    if (!Sa) {
        return;
    }
    Sa->Family = AF_INET;
    Sa->Port = (unsigned short)Port;
    Sa->Addr = Ip;
}

int ToyNetConnectIn(int Fd, const ToySockAddrIn *Sa) {
    if (!Sa || Sa->Family != AF_INET) {
        errno = EINVAL;
        return -1;
    }
    return ToyNetConnect(Fd, Sa->Addr, (unsigned short)Sa->Port);
}

int ToyNetBindIn(int Fd, const ToySockAddrIn *Sa) {
    if (!Sa || Sa->Family != AF_INET) {
        errno = EINVAL;
        return -1;
    }
    return ToyNetBind(Fd, Sa->Addr, (unsigned short)Sa->Port);
}

int ToyNetGetAddrIn(ToySockAddrIn *Sa, const char *Name, unsigned Port) {
    unsigned Ip;

    if (!Sa) {
        errno = EINVAL;
        return -1;
    }
    if (ToyNetResolve(Name, &Ip) != 0) {
        return -1;
    }
    ToyNetAddrIn(Sa, Ip, Port);
    return 0;
}

/* 点分 IPv4 / localhost 本地完成；域名走 SYS_SOCKET type=TOY_NET_SOCK_RESOLVE */
int ToyNetResolve(const char *Name, unsigned *OutIp) {
    unsigned A = 0, B = 0, C = 0, D = 0;
    unsigned I;
    const char *P;
    unsigned *Slot;
    unsigned Acc;
    int Dots;
    ToyNetDnsQuery Query;
    long R;

    if (!Name || !Name[0] || !OutIp) {
        errno = EINVAL;
        return -1;
    }
    if (Name[0] == 'l' && Name[1] == 'o' && Name[2] == 'c' && Name[3] == 'a' &&
        Name[4] == 'l' && Name[5] == 'h' && Name[6] == 'o' && Name[7] == 's' &&
        Name[8] == 't' && Name[9] == 0) {
        *OutIp = ToyNetIpv4(127, 0, 0, 1);
        return 0;
    }
    P = Name;
    Slot = &A;
    Acc = 0;
    Dots = 0;
    for (I = 0;; I++) {
        char Ch = P[I];
        if (Ch >= '0' && Ch <= '9') {
            Acc = Acc * 10u + (unsigned)(Ch - '0');
            if (Acc > 255u) {
                errno = EINVAL;
                return -1;
            }
            continue;
        }
        if (Ch == '.') {
            *Slot = Acc;
            Acc = 0;
            Dots++;
            if (Dots == 1) {
                Slot = &B;
            } else if (Dots == 2) {
                Slot = &C;
            } else if (Dots == 3) {
                Slot = &D;
            } else {
                errno = EINVAL;
                return -1;
            }
            continue;
        }
        if (Ch == 0) {
            if (Dots != 3) {
                break;
            }
            *Slot = Acc;
            *OutIp = ToyNetIpv4(A, B, C, D);
            return 0;
        }
        break;
    }

    memset(&Query, 0, sizeof(Query));
    for (I = 0; Name[I] != 0; I++) {
        if (I >= TOY_NET_NAME_MAX) {
            errno = EINVAL;
            return -1;
        }
        Query.Name[I] = Name[I];
    }
    R = toy_socket(AF_INET, TOY_NET_SOCK_RESOLVE, (long)&Query);
    if (R < 0) {
        ToyNetFail(R == -1 ? (long)-EIO : R);
        return -1;
    }
    *OutIp = Query.Ip;
    return 0;
}
