/*
 * StoreJob.h — Store 后台作业（窗/Shell 同队；Worker 泵）
 *
 * INTERFACE：Enqueue 入队即返回；WorkerTask 调 StoreJobStep。
 * Shell：StoreJobShellRun 只 Enqueue（rm-exc-11）；完成用 store job。
 * 窗：Enqueue 后回 Gui 循环。Cancel 在相位/chunk 边界生效。
 */
#ifndef STORE_JOB_H
#define STORE_JOB_H

#include "BootTypes.h"

typedef enum {
    STORE_JOB_NONE = 0,
    STORE_JOB_INSTALL,
    STORE_JOB_REMOVE,
    STORE_JOB_SYNC,
    STORE_JOB_FETCH
} STORE_JOB_KIND;

/* FinishStatus：用户取消（非 FAT 错） */
#define STORE_JOB_ERR_CANCEL  (-100)

/* 成功 0；已有作业 -1；Kind/Id 非法 -1 */
int StoreJobEnqueue(STORE_JOB_KIND Kind, const char *Id);
/* 0=仍忙/前进；1=本轮结束或空闲（成败看 GetStatus / Store 状态行） */
int StoreJobStep(void);
/* 排队中或执行中（含 Shell 同步互斥） */
int StoreJobIsBusy(void);
/* 仅 UI Job（running/pending），不含 Shell */
int StoreJobUiIsBusy(void);
/* 仅执行中（悬停禁重绘；排队阶段仍可悬停） */
int StoreJobIsRunning(void);
void StoreJobGetStatus(char *Out, int OutMax);
/* 上一趟 UI Job 结束时的 Err（FAT_* / CANCEL）；Enqueue 前勿依赖 */
int StoreJobLastError(void);
/* Busy 时请求取消 UI 作业；0=已受理；-1=空闲或仅 Shell busy */
int StoreJobCancel(void);

/*
 * Shell 互斥（遗留：仅当仍有同步 Store* 路径时用 Begin/End）。
 * fetch 已并入 Job；install/remove/sync/fetch 均走 Enqueue。
 */
int StoreJobShellBegin(void);
void StoreJobShellEnd(void);
int StoreJobShellIsBusy(void);

/*
 * Shell INTERFACE：只 Enqueue，立即返回（0=已入队；-1=忙）。
 * Worker 推进；结果：store job / LastError / Store 窗状态行。
 */
int StoreJobShellRun(STORE_JOB_KIND Kind, const char *Id);

#endif
