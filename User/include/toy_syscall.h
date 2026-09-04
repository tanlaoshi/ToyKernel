/*
 * toy_syscall.h — 用户态系统调用号与薄封装（PR-CRT1）
 * 与内核 Include/Syscall.h 编号一致；用户程序勿包含内核头。
 */
#ifndef TOY_SYSCALL_H
#define TOY_SYSCALL_H

#define SYS_EXIT    0
#define SYS_WRITE   1
#define SYS_OPEN    2
#define SYS_READ    3
#define SYS_CLOSE   4
#define SYS_FORK    5
#define SYS_WAIT    6
#define SYS_YIELD   7
#define SYS_SOCKET  8
#define SYS_CONNECT 9
#define SYS_BIND    10
#define SYS_LISTEN  11
#define SYS_ACCEPT  12

#define WNOHANG 1

typedef long ssize_t;
typedef unsigned long size_t;

long toy_syscall(long n, long a, long b, long c);

static inline long toy_exit(long status) {
    return toy_syscall(SYS_EXIT, status, 0, 0);
}

static inline ssize_t toy_write(long fd, const void *buf, size_t len) {
    return (ssize_t)toy_syscall(SYS_WRITE, fd, (long)buf, (long)len);
}

static inline long toy_open(const char *path) {
    return toy_syscall(SYS_OPEN, (long)path, 0, 0);
}

static inline ssize_t toy_read(long fd, void *buf, size_t len) {
    return (ssize_t)toy_syscall(SYS_READ, fd, (long)buf, (long)len);
}

static inline long toy_close(long fd) {
    return toy_syscall(SYS_CLOSE, fd, 0, 0);
}

static inline long toy_yield(void) {
    return toy_syscall(SYS_YIELD, 0, 0, 0);
}

#endif
