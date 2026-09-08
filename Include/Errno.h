/*
 * Errno.h — 内核与用户态约定的错误码数值（PR-N-dns）
 *
 * 数值对齐常见 Linux/x86；用户头 User/include/errno.h 须保持同号。
 * 系统调用失败可返回 -(Errno)，CRT 再置 errno。
 */
#ifndef TOY_ERRNO_H
#define TOY_ERRNO_H

#define TOY_EPERM            1
#define TOY_ENOENT           2
#define TOY_ESRCH            3
#define TOY_EIO              5
#define TOY_EAGAIN          11
#define TOY_ENOMEM          12
#define TOY_EACCES          13
#define TOY_EEXIST          17
#define TOY_ENOTDIR         20
#define TOY_EINVAL          22
#define TOY_EMFILE          24
#define TOY_ENOSPC          28
#define TOY_EPIPE           32
#define TOY_ECHILD          10
#define TOY_EBADF            9

#define TOY_ENETUNREACH    101
#define TOY_ECONNRESET     104
#define TOY_ENOTCONN       107
#define TOY_ETIMEDOUT      110
#define TOY_ECONNREFUSED   111
#define TOY_EHOSTUNREACH   113
#define TOY_EINPROGRESS    115

#endif
