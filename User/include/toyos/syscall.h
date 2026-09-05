/*
 * toyos/syscall.h — 系统调用号与薄封装（PR-L1；原 toy_syscall.h）
 * 与内核 Include/Syscall.h 编号一致；用户程序勿包含内核头。
 */
#ifndef TOYOS_SYSCALL_H
#define TOYOS_SYSCALL_H

#include <sys/types.h>
#include <toyos/version.h>

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
#define SYS_EXECVE  13
#define SYS_PIPE    14
#define SYS_DUP     15
#define SYS_BRK     16
#define SYS_KILL    17
#define SYS_CREATE_WINDOW 18
#define SYS_DAMAGE        19
#define SYS_POLL_INPUT    20
#define SYS_UI_BUTTON     21

#define WNOHANG 1

#define SIGINT  2
#define SIGKILL 9
#define SIGTERM 15

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

static inline long toy_execve(const char *path, char *const argv[],
                              char *const envp[]) {
    return toy_syscall(SYS_EXECVE, (long)path, (long)argv, (long)envp);
}

static inline long toy_fork(void) {
    return toy_syscall(SYS_FORK, 0, 0, 0);
}

static inline long toy_wait(long options) {
    return toy_syscall(SYS_WAIT, options, 0, 0);
}

static inline long toy_pipe(int pipefd[2]) {
    return toy_syscall(SYS_PIPE, (long)pipefd, 0, 0);
}

static inline long toy_dup(int fd) {
    return toy_syscall(SYS_DUP, fd, 0, 0);
}

static inline long toy_brk(long addr) {
    return toy_syscall(SYS_BRK, addr, 0, 0);
}

static inline long toy_kill(long pid, long sig) {
    return toy_syscall(SYS_KILL, pid, sig, 0);
}

static inline long toy_create_window(const char *title, long w, long h) {
    return toy_syscall(SYS_CREATE_WINDOW, (long)title, w, h);
}

static inline long toy_damage(long wid, const char *text) {
    return toy_syscall(SYS_DAMAGE, wid, (long)text, 0);
}

static inline long toy_poll_input(long wid) {
    return toy_syscall(SYS_POLL_INPUT, wid, 0, 0);
}

static inline long toy_ui_button(long wid, long button_id, const char *label) {
    return toy_syscall(SYS_UI_BUTTON, wid, button_id, (long)label);
}

#endif
