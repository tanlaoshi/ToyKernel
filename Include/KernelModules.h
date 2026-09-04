/*
 * KernelModules.h — 内核子系统模块表与顺序初始化
 */
#ifndef KERNEL_MODULES_H
#define KERNEL_MODULES_H

int KernelModulesRun(void);
/* PR-V5：virt 桌面模块表已选中（有帧缓冲） */
int KernelModulesVirtDesktop(void);

#endif
