/*
 * Kernel.h — 内核公共头文件
 *
 * 声明内核 C 语言入口 KernelEntry，由 ToyBoot 在 ExitBootServices 之后调用。
 */
#ifndef KERNEL_H
#define KERNEL_H

#include "BootConfig.h"

/* 内核主入口：接收 UEFI 引导阶段传入的 BOOT_CONFIG 指针 */
void KernelEntry(BOOT_CONFIG *BootConfig);

#endif
