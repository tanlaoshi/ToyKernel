/*
 * unistd.h — read/write/close/execve/pipe/dup/fork/wait/brk/kill（CRT2 + P1～P4）
 */
#ifndef UNISTD_H
#define UNISTD_H

#include "toy_syscall.h"

ssize_t read(int fd, void *buf, size_t count);
ssize_t write(int fd, const void *buf, size_t count);
int close(int fd);
int execve(const char *path, char *const argv[], char *const envp[]);
int pipe(int pipefd[2]);
int dup(int fd);
pid_t fork(void);
pid_t wait(int *status);
/* PR-P3：addr==0 查询；成功返回新/当前 break，失败 (void*)-1 */
void *brk(void *addr);
/* PR-P4：pid 与 fork 返回值一致；仅 SIGKILL/TERM/INT */
int kill(pid_t pid, int sig);

#endif
