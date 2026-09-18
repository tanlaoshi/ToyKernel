/*
 * Tasks.h — 内核常驻任务入口
 */
#ifndef TASKS_H
#define TASKS_H

#include "BootTypes.h"

void ShellTask(void);
void GuiTask(void);
void WorkerTask(void);
void InputTask(void);   /* PR-S-input-pin 序 2：输入钉专核（SMP≥3 → CPU2） */

UINT32 WorkerLoopCount(void);

#endif
