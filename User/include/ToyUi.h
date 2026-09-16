/*
 * ToyUi.h — 用户态控件薄库（libToyUi）
 *
 * ABI：改签名 / 删已有事件语义须递增 TOY_UI_ABI_VERSION_MAJOR。
 * 1.0.0：窗 / 标签 / 底栏按钮 0..3 / Poll。
 * 1.1.0：+ 复选框 / 列表 / 输入框（用户态绘制；客户区点击经 Poll 分发）。
 * 依赖 libToyGfx + G14/G15 窗口 syscall；不链接内核 UI.c。
 */
#ifndef TOY_UI_H
#define TOY_UI_H

#include <ToyGfx.h>

#define TOY_UI_ABI_VERSION_MAJOR 1
#define TOY_UI_ABI_VERSION_MINOR 1
#define TOY_UI_ABI_VERSION_PATCH 0
#define TOY_UI_ABI_VERSION_STRING "1.1.0"

#define TOY_UI_EVENT_NONE          0
#define TOY_UI_EVENT_CLOSE         1
#define TOY_UI_EVENT_CLICK         2  /* 客户区点击未命中控件 */
#define TOY_UI_EVENT_BUTTON_BASE 100
#define TOY_UI_BUTTON_EVENT(Id) (TOY_UI_EVENT_BUTTON_BASE + (Id))
#define TOY_UI_EVENT_CHECK_BASE 200
#define TOY_UI_CHECK_EVENT(Id)  (TOY_UI_EVENT_CHECK_BASE + (Id))
#define TOY_UI_EVENT_LIST_BASE  220
#define TOY_UI_LIST_EVENT(Id)   (TOY_UI_EVENT_LIST_BASE + (Id))
#define TOY_UI_EVENT_TEXT_BASE  240
#define TOY_UI_TEXT_EVENT(Id)   (TOY_UI_EVENT_TEXT_BASE + (Id))

#define TOY_UI_BUTTON_ID_MIN 0
#define TOY_UI_BUTTON_ID_MAX 3
#define TOY_UI_CHECK_ID_MAX  1
#define TOY_UI_LIST_ID_MAX   0
#define TOY_UI_TEXT_ID_MAX   0
#define TOY_UI_LIST_ITEM_MAX 4
#define TOY_UI_TEXT_MAX      24

/* 成功返回 wid（>=0）；失败 -1 */
int ToyUiCreateWindow(const char *Title, unsigned Width, unsigned Height);
/* 客户区标签（文字 damage）；成功 0，失败 -1 */
int ToyUiSetLabel(int WindowId, const char *Text);
/* ButtonId 0..3；底栏自动排布；成功 0，失败 -1 */
int ToyUiAddButton(int WindowId, int ButtonId, const char *Label);
/*
 * ToyUiPoll — 非阻塞取事件
 * 返回：NONE / CLOSE / CLICK / BUTTON / CHECK / LIST / TEXT；错误 -1
 * 0 / 1 / 100+id 语义与 1.0.0 相同。
 */
int ToyUiPoll(int WindowId);

/* PR-A-ui-api：客户区控件（坐标与 ToyGfx 相同，含 pad） */
int ToyUiAddCheckBox(int WindowId, int CheckId, unsigned X, unsigned Y);
int ToyUiSetCheckBox(int WindowId, int CheckId, int Checked);
int ToyUiGetCheckBox(int WindowId, int CheckId);

int ToyUiAddList(int WindowId, int ListId, unsigned X, unsigned Y, unsigned W);
int ToyUiListAddItem(int WindowId, int ListId, const char *Text);
int ToyUiListSetSelected(int WindowId, int ListId, int Index);
int ToyUiListGetSelected(int WindowId, int ListId);

int ToyUiAddTextField(int WindowId, int FieldId, unsigned X, unsigned Y,
                      unsigned W);
int ToyUiSetTextField(int WindowId, int FieldId, const char *Text);
int ToyUiGetTextField(int WindowId, int FieldId, char *Buf, unsigned Cap);

#endif
