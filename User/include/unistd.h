/*
 * unistd.h — read/write/close（PR-CRT2）
 */
#ifndef UNISTD_H
#define UNISTD_H

#include "toy_syscall.h"

ssize_t read(int fd, void *buf, size_t count);
ssize_t write(int fd, const void *buf, size_t count);
int close(int fd);

#endif
