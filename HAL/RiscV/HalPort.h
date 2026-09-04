#ifndef HAL_PORT_H
#define HAL_PORT_H

#include "BootTypes.h"

#define HAL_VEC_XHCI    16
#define HAL_VEC_TIMER   5
#define HAL_VEC_SYSCALL 8

#define VEC_XHCI  HAL_VEC_XHCI
#define VEC_TIMER HAL_VEC_TIMER
#define VEC_SYSCALL HAL_VEC_SYSCALL

typedef struct HAL_FRAME {
    UINT64 X[32];
    UINT64 Vec;
    UINT64 Err;
    /* PR-A15：中立名（sepc / sstatus / 用户 sp 由 TrapVec.S 填） */
    UINT64 InstructionPointer;
    UINT64 Cs;
    UINT64 Rflags;
    UINT64 StackPointer;
    UINT64 Ss;
} HAL_FRAME;

typedef HAL_FRAME INT_FRAME;

#endif
