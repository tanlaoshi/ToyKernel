/*
 * signal.h — PR-P4 + PR-U-sig：kill / signal（教学子集，无 sigaction）
 */
#ifndef SIGNAL_H
#define SIGNAL_H

#include <sys/types.h>
#include <toyos/syscall.h>

typedef void (*sighandler_t)(int);

#define SIG_ERR ((sighandler_t)-1)
#define SIG_DFL ((sighandler_t)0)
#define SIG_IGN ((sighandler_t)1)

/*
 * SIG_DFL=默认终止；SIG_IGN=忽略（不可用于 SIGKILL）；
 * 自定义 handler：内核改用户帧进入，ret 回中断点。
 */
sighandler_t signal(int sig, sighandler_t handler);
int kill(pid_t pid, int sig);

#endif
