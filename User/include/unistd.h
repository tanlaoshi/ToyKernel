/*
 * unistd.h — read/write/close/execve/pipe/dup/fork/wait（CRT2 + P1/P2）
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

#endif
