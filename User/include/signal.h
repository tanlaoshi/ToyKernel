/*
 * signal.h — PR-P4 简单信号（默认终止；无 sigaction / mask）
 */
#ifndef SIGNAL_H
#define SIGNAL_H

#include "toy_syscall.h"

#define SIGINT  2
#define SIGKILL 9
#define SIGTERM 15

int kill(pid_t pid, int sig);

#endif
