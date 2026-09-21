/*
 * DevicesUi.h — 设备管理器 GUI（PR-DEV-6；只读 Device 表）
 */
#ifndef DEVICES_UI_H
#define DEVICES_UI_H

#include "BootTypes.h"

void DevicesUiOpen(void);
void DevicesUiPaintFocused(void);
void DevicesUiRepaint(void);
void DevicesUiOnClick(UINT32 X, UINT32 Y);
int DevicesUiIsFocused(void);

#endif
