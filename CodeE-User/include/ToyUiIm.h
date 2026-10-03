/*
 * ToyUiIm.h — 立即模式薄封装（不改 ToyUi.h ABI）
 *
 * 用法（每帧）：
 *   ToyUiImBegin(Wid, 0);
 *   if (ToyUiImClosed()) break;
 *   ToyUiImLabel("...");
 *   (void)ToyUiImCheck(&On);
 *   if (ToyUiImButton("OK")) { ... }
 *   ToyUiImEnd();
 *   sched_yield();  -- 每帧必让出，勿只在 Idle 时 yield
 */
#ifndef TOY_UI_IM_H
#define TOY_UI_IM_H

#include <ToyUi.h>

#define TOY_UI_IM_BTN_MAX (TOY_UI_BUTTON_ID_MAX - TOY_UI_BUTTON_ID_MIN + 1)
#define TOY_UI_IM_CHK_MAX (TOY_UI_CHECK_ID_MAX + 1)
#define TOY_UI_IM_LAB_MAX 24

/* 每帧开头：Poll 一次；Title 非空则 SetLabel。成功 0，Wid 无效 -1 */
int ToyUiImBegin(int Wid, const char *Title);

/* 本帧是否关窗（须在 Begin 之后问） */
int ToyUiImClosed(void);

/* 本帧无事件（便于 yield） */
int ToyUiImIdle(void);

/* 客户区标签（变化才 SetLabel）。成功 0 */
int ToyUiImLabel(const char *Text);

/*
 * 声明一颗底栏按钮（id 按本帧出现顺序 0..3）。
 * 返回 1 = 本帧按下；0 = 未按 / 槽满 / 未 Begin。
 */
int ToyUiImButton(const char *Label);

/*
 * 客户区复选框（id 按本帧出现顺序 0..CHECK_ID_MAX；固定坐标）。
 * *Checked 与控件同步；返回 1 = 本帧点了。
 */
int ToyUiImCheck(int *Checked);

/* 帧末：固化本帧按钮/复选表 */
void ToyUiImEnd(void);

#endif
