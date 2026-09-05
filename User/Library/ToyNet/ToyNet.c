/*
 * ToyNet.c — libToyNet（PR-L4）
 */
#include <errno.h>
#include <ToyNet.h>
#include <unistd.h>
#include <toyos/syscall.h>

int socket(int domain, int type, int protocol) {
    long r = toy_socket(domain, type, protocol);
    if (r < 0) {
        errno = EIO;
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
        errno = EIO;
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
        errno = EIO;
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
        errno = EIO;
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
        errno = EIO;
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
