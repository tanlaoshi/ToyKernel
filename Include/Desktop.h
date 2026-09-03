/*
 * Desktop.h — 桌面图标与双击打开（PR-D4）
 */
#ifndef DESKTOP_H
#define DESKTOP_H

#include "BootTypes.h"

void DesktopInit(void);
/* 在桌面背景上画图标（GuiRedraw / 关窗露底后调用） */
void DesktopDraw(void);
/* 仅重绘与矩形相交的图标（关窗擦除区域） */
void DesktopDrawRect(UINT32 X, UINT32 Y, UINT32 W, UINT32 H);
/*
 * 桌面空白处的按下：双击图标则打开对应窗并返回 1；
 * 单击图标只记录待双击状态返回 1；点空白清除选择返回 0。
 */
int DesktopHandleClick(UINT32 X, UINT32 Y);

#endif
