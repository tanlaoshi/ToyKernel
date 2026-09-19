/*
 * TasksPrivate.h — 常驻任务内部（仅 Common/Services/Tasks；User 勿 include）
 */
#ifndef TASKS_PRIVATE_H
#define TASKS_PRIVATE_H

#include "Tasks.h"
#include "HalDevices.h"

void YieldForPollInput(void);
void FeedHid(HAL_KEYBOARD_REPORT *Report, HAL_KEYBOARD_REPORT *Previous);

#endif
