/*
 * StoreJob.h — Store UI 后台作业（PR-S-job）
 *
 * 协作切片：Enqueue 入队，GuiPollMouse 末尾 StoreJobStep 推进。
 * 序 5：Cancel 在相位/chunk 边界生效；半截拷贝 Abort 不登记。
 * Shell 同步 Store* API 不变；本头供 StoreUi / 日后 Shell 互斥用。
 */
#ifndef STORE_JOB_H
#define STORE_JOB_H

#include "BootTypes.h"

typedef enum {
    STORE_JOB_NONE = 0,
    STORE_JOB_INSTALL,
    STORE_JOB_REMOVE,
    STORE_JOB_SYNC
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
/* Busy 时请求取消 UI 作业；0=已受理；-1=空闲或仅 Shell busy */
int StoreJobCancel(void);

/*
 * Shell 同步路径互斥（PR-S-job-shell）：
 * Begin 失败 = UI 作业进行中；成功后 UI 见 IsBusy，勿与 UI Job 并行。
 * Shell 仍直接调 StoreInstall/Combo*（保持同步），不改走 Step。
 */
int StoreJobShellBegin(void);
void StoreJobShellEnd(void);
int StoreJobShellIsBusy(void);

#endif
