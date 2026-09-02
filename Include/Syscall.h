#ifndef SYSCALL_H
#define SYSCALL_H

#include "BootTypes.h"
#include "Hal.h"

#define SYS_EXIT  0
#define SYS_WRITE 1
#define SYS_OPEN  2
#define SYS_READ  3
#define SYS_CLOSE 4
#define SYS_FORK  5
#define SYS_WAIT  6
#define SYS_YIELD 7

/* SYS_WAIT：rdi = options；WNOHANG 时无已退出子进程则返回 0（不阻塞） */
#define WNOHANG 1

void SyscallInit(void);
UINT64 SyscallDispatch(HAL_FRAME *Frame);

#endif
