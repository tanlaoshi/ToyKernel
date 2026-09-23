/*
 * sched.h — POSIX 调度（PR-U-sched-yield）
 * 第 1 轨：sched_yield 让出 CPU；走已有 SYS_YIELD（7），不新增 syscall 号。
 * 第 2 轨旧名 toy_yield 保留为宏别名（见 toyos/syscall.h）。
 */
#ifndef SCHED_H
#define SCHED_H

int sched_yield(void);

#endif
