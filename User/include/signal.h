/*
 * signal.h — PR-P4 + PR-L2：常量 + signal() 薄封装（无 handler 投递）
 */
#ifndef SIGNAL_H
#define SIGNAL_H

#include <sys/types.h>
#include <toyos/syscall.h>

typedef void (*sighandler_t)(int);

#define SIG_ERR ((sighandler_t)-1)
#define SIG_DFL ((sighandler_t)0)
#define SIG_IGN ((sighandler_t)1)

/* 仅接受 SIG_DFL / SIG_IGN；自定义 handler → SIG_ERR（内核无用户 handler） */
sighandler_t signal(int sig, sighandler_t handler);
int kill(pid_t pid, int sig);

#endif
