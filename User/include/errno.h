/*
 * errno.h — 用户态错误码（PR-CRT2 / PR-N-dns）
 *
 * 数值与 Include/Errno.h（TOY_*）一致；socket 失败可经 syscall 返回 -errno。
 */
#ifndef ERRNO_H
#define ERRNO_H

extern int errno;

#define EPERM   1
#define ENOENT  2
#define ESRCH   3
#define EIO     5
#define EBADF   9
#define ECHILD  10
#define EAGAIN  11
#define ENOMEM  12
#define EACCES  13
#define EEXIST  17
#define ENOTDIR 20
#define EINVAL  22
#define EMFILE  24
#define ENOSPC  28
#define EPIPE   32

#define ENETUNREACH  101
#define ECONNRESET   104
#define ENOTCONN     107
#define ETIMEDOUT    110
#define ECONNREFUSED 111
#define EHOSTUNREACH 113
#define EINPROGRESS  115

#endif
