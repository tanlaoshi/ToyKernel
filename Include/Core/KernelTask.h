/*
 * KernelTask.h — 通用内核任务登记表（最多 4 槽）
 *
 * 人话：给「慢一点的内核活」挂个名字、状态、进度，给任务管理器看。
 * 商店装卸不走这里，走 Worker。
 *
 * 从哪读：KernelTaskRegister；开机演示见 KernelTaskDemoStart。
 */
#ifndef KERNEL_TASK_H
#define KERNEL_TASK_H

#define KERNEL_TASK_SLOTS 4

typedef enum {
    KERNEL_TASK_FREE = 0,
    KERNEL_TASK_RUN,
    KERNEL_TASK_DONE
} KERNEL_TASK_STATE;

int KernelTaskRegister(const char *Name, void (*Fn)(void *), void *Ctx);
void KernelTaskSetProgress(int Slot, int Progress, const char *Message);
int KernelTaskState(int Slot);
int KernelTaskProgress(int Slot);

/* 开机拉起教学慢任务，ps / 任务管理器里名字是 KernelTaskDemo */
void KernelTaskDemoStart(void);

#endif
