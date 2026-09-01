#ifndef SYSCALL_H
#define SYSCALL_H

#include "BootTypes.h"

struct INT_FRAME;

#define SYS_EXIT  0
#define SYS_WRITE 1
#define SYS_OPEN  2
#define SYS_READ  3
#define SYS_CLOSE 4
#define SYS_FORK  5
#define SYS_WAIT  6
#define SYS_YIELD 7

void SyscallInit(void);
UINT64 SyscallDispatch(struct INT_FRAME *Frame);

#endif
