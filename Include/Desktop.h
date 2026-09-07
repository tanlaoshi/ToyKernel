/*
 * Desktop.h — 桌面图标、任务栏与壁纸（PR-D4 / PR-G13）
 *
 * PR-R2：Desktop 不 #include Gui/Console；只发 DESKTOP_ACTION，由 Gui 开窗。
 */
#ifndef DESKTOP_H
#define DESKTOP_H

#include "BootTypes.h"

typedef enum {
    DESKTOP_ACTION_NONE = -1,
    DESKTOP_ACTION_SHELL = 0,
    DESKTOP_ACTION_SETTINGS,
    DESKTOP_ACTION_FILES
} DESKTOP_ACTION;

void DesktopInit(void);
/* Gui 注册：点是否被窗占用（图标避让）；菜单开合时请求刷新桌面 */
void DesktopSetPointOccupied(int (*Fn)(UINT32 X, UINT32 Y));
void DesktopSetRequestRefresh(void (*Fn)(void));
/* 在桌面背景上画图标+任务栏（GuiRedraw / 关窗露底后调用） */
void DesktopDraw(void);
/* 仅重绘与矩形相交的图标/任务栏（关窗擦除区域） */
void DesktopDrawRect(UINT32 X, UINT32 Y, UINT32 W, UINT32 H);
/* PR-G13：壁纸或 ThemeDesktopBackground 填矩形 */
void DesktopFillRect(UINT32 X, UINT32 Y, UINT32 W, UINT32 H);
UINT32 DesktopBgAt(UINT32 X, UINT32 Y);
/*
 * 采样桌面图标/任务栏像素。
 * 命中不透明像素返回 1 并写 *Out；否则返回 0（调用方用 DesktopBgAt）。
 */
int DesktopSamplePixel(UINT32 X, UINT32 Y, UINT32 *Out);
/*
 * 桌面空白处的按下：任务栏/开始菜单，或双击图标。
 * 返回 1=已处理；*OutAction 为待开应用（NONE 表示仅菜单/选中）。
 */
int DesktopHandleClick(UINT32 X, UINT32 Y, DESKTOP_ACTION *OutAction);
void DesktopRefreshLabels(void);

#endif
