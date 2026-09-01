#ifndef HAL_PORT_H
#define HAL_PORT_H

#include "BootTypes.h"

#define HAL_VEC_XHCI    32
#define HAL_VEC_TIMER   30
#define HAL_VEC_SYSCALL 0

#define VEC_XHCI  HAL_VEC_XHCI
#define VEC_TIMER HAL_VEC_TIMER
#define VEC_SYSCALL HAL_VEC_SYSCALL

typedef struct INT_FRAME {
    UINT64 X[31];
    UINT64 Vec;
    UINT64 Err;
    UINT64 Rip;
    UINT64 Cs;
    UINT64 Rflags;
    UINT64 Rsp;
    UINT64 Ss;
} INT_FRAME;

typedef INT_FRAME HAL_FRAME;

#endif
