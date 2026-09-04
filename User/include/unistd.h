/*
 * unistd.h — read/write/close/execve（PR-CRT2 + PR-P1）
 */
#ifndef UNISTD_H
#define UNISTD_H

#include "toy_syscall.h"

ssize_t read(int fd, void *buf, size_t count);
ssize_t write(int fd, const void *buf, size_t count);
int close(int fd);
/* 成功不返回；失败 -1 并置 errno */
int execve(const char *path, char *const argv[], char *const envp[]);

#endif
