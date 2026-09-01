/*
 * HAL/x86_64/hal_port.h — x86-64 架构常量与 HAL_FRAME 布局
 */
#ifndef HAL_PORT_H
#define HAL_PORT_H

#include "BootConfig.h"

#define HAL_VEC_XHCI   0x40
#define HAL_VEC_TIMER  0x41
#define HAL_VEC_SYSCALL 0x80

#define VEC_XHCI  HAL_VEC_XHCI
#define VEC_TIMER HAL_VEC_TIMER

typedef struct INT_FRAME {
    UINT64 Rax, Rbx, Rcx, Rdx, Rsi, Rdi, Rbp;
    UINT64 R8, R9, R10, R11, R12, R13, R14, R15;
    UINT64 Vector, ErrorCode;
    UINT64 Rip, Cs, Rflags, Rsp, Ss;
} INT_FRAME;

typedef INT_FRAME HAL_FRAME;

void ArchIdtSetGate(UINT32 Vec, void *Handler, UINT8 Type);
int ArchInit(void);
void ArchSetRsp0(UINT64 Rsp0);
void ArchTssInstall(void);
void ArchSti(void);
void ArchCli(void);
void LapicEoi(void);
void TimerStart(void);
UINT64 InterruptDispatch(INT_FRAME *Frame);
void SchedulerEnter(INT_FRAME *Frame);
void KernelEnter(INT_FRAME *Frame);
void UserEnter(INT_FRAME *Frame);

#define VEC_SYSCALL HAL_VEC_SYSCALL

#endif
