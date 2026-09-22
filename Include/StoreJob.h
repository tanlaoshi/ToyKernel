/*
 * StoreJob.h — Store UI 后台作业（PR-S-job）
 *
 * 协作切片：Enqueue 入队，GuiPollMouse 末尾 StoreJobStep 推进。
 * 序 1：Step 一次跑完旧 Combo/Sync（行为同现网）；相位/chunk 后续刀。
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

/* 成功 0；已有作业 -1；Kind/Id 非法 -1 */
int StoreJobEnqueue(STORE_JOB_KIND Kind, const char *Id);
/* 0=仍忙/前进；1=本轮结束或空闲（成败看 GetStatus / Store 状态行） */
int StoreJobStep(void);
/* 排队中或执行中 */
int StoreJobIsBusy(void);
/* 仅执行中（悬停禁重绘；排队阶段仍可悬停） */
int StoreJobIsRunning(void);
void StoreJobGetStatus(char *Out, int OutMax);
/* 序 5 前：固定 -1 */
int StoreJobCancel(void);

#endif
