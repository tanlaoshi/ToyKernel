/*
 * KernelModules.h — 内核子系统模块表与顺序初始化
 *
 * 人话：开机时要跑哪些子系统、按什么顺序，三张表、三个 Run。
 * 从哪读：KernelModulesRunVirt / RunVirtDesktop / RunFull。
 * 别改：表内顺序（USB 在 FileSystem 前等）；选哪张表由 KernelMain 分发。
 */
#ifndef KERNEL_MODULES_H
#define KERNEL_MODULES_H

int KernelModulesRunVirt(void);
int KernelModulesRunVirtDesktop(void);
int KernelModulesRunFull(void);

/* 1 = 已选桌面模块表（有帧缓冲且非 ConsoleOnly） */
int KernelModulesVirtDesktop(void);

#endif
