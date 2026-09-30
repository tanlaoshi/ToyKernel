/*
 * StoreUi.h — 商店 GUI 三分栏（类 Files）
 *
 * 左：All / Apps / Fonts / Assets / Installed
 * 中：过滤后的包列表；右：详情 + Install/Remove/Sync
 */
#ifndef STORE_UI_H
#define STORE_UI_H

#include "BootTypes.h"

void StoreUiOpen(void);
void StoreUiPaintFocused(void);
void StoreUiRepaint(void);
void StoreUiOnClick(UINT32 X, UINT32 Y);
/* 与 Settings 一致：悬停/按下高亮，抬起触发 */
void StoreUiOnPointer(UINT32 X, UINT32 Y, UINT8 Buttons);
void StoreUiOnEscape(void);
/* 鼠标轮询末尾：推进 StoreJob（勿在 OnPointer 里同步 IO） */
void StoreUiPump(void);
int StoreUiIsFocused(void);
/* 作业排队或执行中：跳过悬停重绘，只让光标跟着走 */
int StoreUiIsBusy(void);

#endif
