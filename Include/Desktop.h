/*
 * Desktop.h — 桌面图标、任务栏与壁纸（PR-D4 / PR-G13）
 */
#ifndef DESKTOP_H
#define DESKTOP_H

#include "BootTypes.h"

void DesktopInit(void);
/* 在桌面背景上画图标+任务栏（GuiRedraw / 关窗露底后调用） */
void DesktopDraw(void);
/* 仅重绘与矩形相交的图标/任务栏（关窗擦除区域） */
void DesktopDrawRect(UINT32 X, UINT32 Y, UINT32 W, UINT32 H);
/* PR-G13：壁纸或 ThemeDesktopBg 填矩形 */
void DesktopFillRect(UINT32 X, UINT32 Y, UINT32 W, UINT32 H);
UINT32 DesktopBgAt(UINT32 X, UINT32 Y);
/*
 * 采样桌面图标/任务栏像素。
 * 命中不透明像素返回 1 并写 *Out；否则返回 0（调用方用 DesktopBgAt）。
 */
int DesktopSamplePixel(UINT32 X, UINT32 Y, UINT32 *Out);
/*
 * 桌面空白处的按下：任务栏/开始菜单，或双击图标打开窗。
 */
int DesktopHandleClick(UINT32 X, UINT32 Y);
void DesktopRefreshLabels(void);

#endif
