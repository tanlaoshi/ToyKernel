/*
 * ToyUi.h — 用户态控件薄库（PR-G15 / libToyUi）
 * 依赖 libToyGfx + G14/G15 窗口 syscall；不链内核 UI.c。
 */
#ifndef TOY_UI_H
#define TOY_UI_H

#include "ToyGfx.h"

#define TOY_UI_EVENT_NONE   0
#define TOY_UI_EVENT_CLOSE  1
#define TOY_UI_EVENT_BUTTON_BASE 100
#define TOY_UI_BUTTON_EVENT(Id) (TOY_UI_EVENT_BUTTON_BASE + (Id))

int ToyUiCreateWindow(const char *Title, unsigned Width, unsigned Height);
int ToyUiSetLabel(int WindowId, const char *Text);
/* ButtonId 0..3；底栏自动排布 */
int ToyUiAddButton(int WindowId, int ButtonId, const char *Label);
/* 见 TOY_UI_EVENT_* */
int ToyUiPoll(int WindowId);

#endif
