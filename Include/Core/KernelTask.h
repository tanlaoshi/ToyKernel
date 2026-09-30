#ifndef KERNEL_TASK_H
#define KERNEL_TASK_H

/*
 * PR-GUI-kerneltask：通用内核任务注册表（≤4 槽）。
 * 慢操作登记 Name/State/Progress/Message，经 SchedulerCreateKernel 跑 Fn(Ctx)。
 * Store 的 WorkerTask/StoreJob 不改用本表。
 */

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

/* 演示慢任务：分步让出，结束打 kerneltask: demo done */
void KernelTaskDemoStart(void);

#endif
