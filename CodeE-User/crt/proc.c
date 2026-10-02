/*
 * proc.c — getpid / getppid（PR-U-getpid；SYS_GETPID / SYS_GETPPID）
 * unistd.c 已 288 行，故单列。
 */
#include <unistd.h>
#include <toyos/syscall.h>

pid_t getpid(void) {
    return (pid_t)toy_syscall(SYS_GETPID, 0, 0, 0);
}

pid_t getppid(void) {
    return (pid_t)toy_syscall(SYS_GETPPID, 0, 0, 0);
}
