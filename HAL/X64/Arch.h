/*
 * Arch.h — x86-64 架构相关接口（仅 HAL 内部；Common 经 Hal* 访问）
 *
 * 中断向量：VEC_XHCI=0x40（USB MSI-X），VEC_TIMER=0x41（LAPIC 定时器）。
 * 中断桩在 Isr.S；C 侧逻辑在 Arch.c。
 */
#ifndef ARCH_H
#define ARCH_H

#include "HalPort.h"

void ArchIdtSetGate(UINT32 Vec, void *Handler, UINT8 Type);
int ArchInit(void);
void ArchApInit(UINT32 LogicalCpu); /* AP：本核 GDT/TSS + 共享 IDT + LAPIC */
void ArchSetRsp0(UINT64 Rsp0);
void ArchTssInstall(void);
void ArchSyscallMsrInit(UINT32 LogicalCpu); /* SYSCALL/SYSRET MSR（与 int 0x80 独立） */
void ArchSti(void);
void ArchCli(void);
void LapicEoi(void);
void TimerStart(void);
UINT64 InterruptDispatch(HAL_FRAME *Frame);
void SchedulerEnter(HAL_FRAME *Frame);
void KernelEnter(HAL_FRAME *Frame);
void UserEnter(HAL_FRAME *Frame);

#endif
