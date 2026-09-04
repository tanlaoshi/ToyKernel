/*
 * unistd.c — open/read/write/close，失败置 errno（PR-CRT2）
 */
#include "errno.h"
#include "fcntl.h"
#include "unistd.h"
#include "toy_syscall.h"

int open(const char *path, int flags) {
    long r;

    (void)flags;
    if (!path || !path[0]) {
        errno = EINVAL;
        return -1;
    }
    r = toy_open(path);
    if (r < 0) {
        errno = ENOENT;
        return -1;
    }
    return (int)r;
}

ssize_t read(int fd, void *buf, size_t count) {
    long r;

    if (fd < 0 || !buf) {
        errno = EINVAL;
        return -1;
    }
    r = (long)toy_read(fd, buf, count);
    if (r < 0) {
        errno = EIO;
        return -1;
    }
    return (ssize_t)r;
}

ssize_t write(int fd, const void *buf, size_t count) {
    long r;

    if (fd < 0 || (!buf && count > 0)) {
        errno = EINVAL;
        return -1;
    }
    r = (long)toy_write(fd, buf, count);
    if (r < 0) {
        errno = EIO;
        return -1;
    }
    return (ssize_t)r;
}

int close(int fd) {
    long r;

    if (fd < 0) {
        errno = EBADF;
        return -1;
    }
    r = toy_close(fd);
    if (r < 0) {
        errno = EBADF;
        return -1;
    }
    return 0;
}
