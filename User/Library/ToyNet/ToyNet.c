/*
 * ToyNet.c — libToyNet（PR-L4 / PR-N-dns）
 */
#include <errno.h>
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

int connect(int fd, unsigned ip, unsigned port) {
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

int bind(int fd, unsigned ip, unsigned port) {
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

/* 点分 IPv4 / localhost；名字解析见 Shell dns（内核 lwIP DNS） */
int ToyNetResolve(const char *Name, unsigned *OutIp) {
    unsigned A = 0, B = 0, C = 0, D = 0;
    unsigned I;
    const char *P;
    unsigned *Slot;
    unsigned Acc;
    int Dots;

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
                errno = EINVAL;
                return -1;
            }
            *Slot = Acc;
            *OutIp = ToyNetIpv4(A, B, C, D);
            return 0;
        }
        errno = EINVAL;
        return -1;
    }
}
