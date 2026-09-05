/*
 * ToyUi.h — 用户态控件薄库（libToyUi）
 *
 * ABI（PR-L3）：改签名 / 事件语义须递增 TOY_UI_ABI_VERSION_MAJOR。
 * 依赖 libToyGfx + G14/G15 窗口 syscall；不链接内核 UI.c。
 */
#ifndef TOY_UI_H
#define TOY_UI_H

#include <ToyGfx.h>

#define TOY_UI_ABI_VERSION_MAJOR 1
#define TOY_UI_ABI_VERSION_MINOR 0
#define TOY_UI_ABI_VERSION_PATCH 0
#define TOY_UI_ABI_VERSION_STRING "1.0.0"

#define TOY_UI_EVENT_NONE          0
#define TOY_UI_EVENT_CLOSE         1
#define TOY_UI_EVENT_BUTTON_BASE 100
#define TOY_UI_BUTTON_EVENT(Id) (TOY_UI_EVENT_BUTTON_BASE + (Id))

#define TOY_UI_BUTTON_ID_MIN 0
#define TOY_UI_BUTTON_ID_MAX 3

/* 成功返回 wid（>=0）；失败 -1 */
int ToyUiCreateWindow(const char *Title, unsigned Width, unsigned Height);
/* 客户区标签（文字 damage）；成功 0，失败 -1 */
int ToyUiSetLabel(int WindowId, const char *Text);
/* ButtonId 0..3；底栏自动排布；成功 0，失败 -1 */
int ToyUiAddButton(int WindowId, int ButtonId, const char *Label);
/*
 * ToyUiPoll — 非阻塞取事件
 * 返回：TOY_UI_EVENT_NONE / CLOSE / TOY_UI_BUTTON_EVENT(id)；错误 -1
 */
int ToyUiPoll(int WindowId);

#endif
