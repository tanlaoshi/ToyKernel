/*
 * SettingsUi.h — Settings 三分栏（类 Files）
 *
 * 左：Desktop / Shell / Font / Display / Language / Scale
 * 中：该类下条目；点选即应用（数字键 1.. 亦可）
 * 右：详情 / 色样 / Now 分辨率提示
 */
#ifndef SETTINGS_UI_H
#define SETTINGS_UI_H

#include "BootTypes.h"

void SettingsUiOpen(void);
void SettingsUiRefresh(void);
void SettingsUiRepaint(void);
void SettingsUiPaintFocused(void);
void SettingsUiOnDigit(char Digit);
void SettingsUiOnEscape(void);
/* PR-G12：客户区鼠标点选（与 OnDigit 等价） */
void SettingsUiOnClick(UINT32 X, UINT32 Y);
/* PR-GUI-l3：客户区指针移动/按住 → 悬停与按下高亮 */
void SettingsUiOnPointer(UINT32 X, UINT32 Y, UINT8 Buttons);
int SettingsUiIsFocused(void);

#endif
