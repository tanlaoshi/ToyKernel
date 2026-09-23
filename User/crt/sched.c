/*
 * sched.c — sched_yield（PR-U-sched-yield）
 * 直接 toy_syscall(SYS_YIELD)；勿调 toy_yield 宏（会递归回本函数）。
 */
#include <sched.h>
#include <toyos/syscall.h>

int sched_yield(void) {
    return (int)toy_syscall(SYS_YIELD, 0, 0, 0);
}
