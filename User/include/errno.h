/*
 * errno.h — 用户态错误码（PR-CRT2）
 * 内核暂统一返回 -1；CRT 映射为下列常见码。
 */
#ifndef ERRNO_H
#define ERRNO_H

extern int errno;

#define EPERM   1
#define ENOENT  2
#define EIO     5
#define EBADF   9
#define ENOMEM  12
#define EACCES  13
#define EEXIST  17
#define EINVAL  22
#define ENOSPC  28
#define EMFILE  24
#define EAGAIN  11
#define ECHILD  10
#define ESRCH   3

#endif
