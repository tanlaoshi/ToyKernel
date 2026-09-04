/*
 * HAL/X86_64/HalPort.h — 对 Common 可见的架构布局（无 Arch* 原型，PR-A5）
 */
#ifndef HAL_PORT_H
#define HAL_PORT_H

#include "BootTypes.h"

#define HAL_VEC_XHCI    0x40
#define HAL_VEC_TIMER   0x41
#define HAL_VEC_SYSCALL 0x80

#define VEC_XHCI    HAL_VEC_XHCI
#define VEC_TIMER   HAL_VEC_TIMER
#define VEC_SYSCALL HAL_VEC_SYSCALL

typedef struct HAL_FRAME {
    UINT64 Rax, Rbx, Rcx, Rdx, Rsi, Rdi, Rbp;
    UINT64 R8, R9, R10, R11, R12, R13, R14, R15;
    UINT64 Vector, ErrorCode;
    UINT64 Rip, Cs, Rflags, Rsp, Ss;
} HAL_FRAME;

typedef HAL_FRAME INT_FRAME;

#endif
