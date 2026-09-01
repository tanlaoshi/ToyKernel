/*
 * Tasks.h — 内核常驻任务入口
 */
#ifndef TASKS_H
#define TASKS_H

#include "BootTypes.h"

void ShellTask(void);
void GuiTask(void);
void WorkerTask(void);

UINT32 WorkerLoopCount(void);

#endif
