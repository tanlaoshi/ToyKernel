/*
 * toyos/syscall.h — 系统调用号与薄封装（PR-L1；原 toy_syscall.h）
 * 与内核 Include/Syscall.h 编号一致；用户程序勿包含内核头。
 * 汇编（User/Apps 下 .S）可 include 本头只取 SYS_*（见 __ASSEMBLER__）。
 */
#ifndef TOYOS_SYSCALL_H
#define TOYOS_SYSCALL_H

/* SYS_* 号段定义见 SyscallABI.h（段内双轨 + 预留区） */
#include "../../../Include/SyscallABI.h"

#ifndef __ASSEMBLER__

#include <sys/types.h>
#include <toyos/version.h>
#include <sched.h>

long toy_syscall(long n, long a, long b, long c);
/* 与 toy_syscall 同一条指令；第二返回值在 rdx / x1 / a1（wait 退出码） */
typedef struct {
    long A;
    long B;
} TOY_RET2;
TOY_RET2 toy_syscall2(long n, long a, long b, long c);

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

static inline long toy_lseek(long fd, long offset, long whence) {
    return toy_syscall(SYS_LSEEK, fd, offset, whence);
}

/* PR-U-sched-yield：实现迁至 User/crt/sched.c 的 sched_yield；保留旧名为宏别名 */
#define toy_yield() sched_yield()

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

static inline long toy_damage_rect(long wid, const void *desc) {
    return toy_syscall(SYS_DAMAGE_RECT, wid, (long)desc, 0);
}

static inline long toy_poll_input(long wid) {
    return toy_syscall(SYS_POLL_INPUT, wid, 0, 0);
}

static inline long toy_ui_button(long wid, long button_id, const char *label) {
    return toy_syscall(SYS_UI_BUTTON, wid, button_id, (long)label);
}

static inline long toy_file_stat(const char *path, void *out) {
    return toy_syscall(SYS_FILE_STAT, (long)path, (long)out, 0);
}

static inline long toy_open_directory(const char *path) {
    return toy_syscall(SYS_OPEN_DIRECTORY, (long)path, 0, 0);
}

static inline long toy_read_directory(long dirfd, void *out) {
    return toy_syscall(SYS_READ_DIRECTORY, dirfd, (long)out, 0);
}

static inline long toy_mmap(long len, long prot, long flags) {
    return toy_syscall(SYS_MMAP, len, prot, flags);
}

static inline long toy_munmap(long addr, long len) {
    return toy_syscall(SYS_MUNMAP, addr, len, 0);
}

static inline long toy_signal(long sig, long handler) {
    return toy_syscall(SYS_SIGNAL, sig, handler, 0);
}

static inline long toy_sleep(long ms) {
    return toy_syscall(SYS_SLEEP, ms, 0, 0);
}

static inline long toy_clock_ms(void) {
    return toy_syscall(SYS_CLOCK_MS, 0, 0, 0);
}

static inline long toy_socket(long domain, long type, long protocol) {
    return toy_syscall(SYS_SOCKET, domain, type, protocol);
}

static inline long toy_connect(long fd, long ip, long port) {
    return toy_syscall(SYS_CONNECT, fd, ip, port);
}

static inline long toy_bind(long fd, long ip, long port) {
    return toy_syscall(SYS_BIND, fd, ip, port);
}

static inline long toy_listen(long fd, long backlog) {
    return toy_syscall(SYS_LISTEN, fd, backlog, 0);
}

static inline long toy_accept(long fd) {
    return toy_syscall(SYS_ACCEPT, fd, 0, 0);
}

#endif /* !__ASSEMBLER__ */

#endif /* TOYOS_SYSCALL_H */
