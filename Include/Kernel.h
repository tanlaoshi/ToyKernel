/*
 * Kernel.h — 内核 Common 入口（架构无关）
 */
#ifndef KERNEL_H
#define KERNEL_H

/* 由 HAL/<Arch>/Startup 在构造 BOOT_INFO 后调用 */
void KernelMain(void);

#endif
