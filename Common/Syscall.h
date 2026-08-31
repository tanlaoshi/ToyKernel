#ifndef SYSCALL_H
#define SYSCALL_H

#include "BootConfig.h"

struct INT_FRAME;

#define SYS_EXIT  0
#define SYS_WRITE 1

void SyscallInit(void);
UINT64 SyscallDispatch(struct INT_FRAME *Frame);

#endif
