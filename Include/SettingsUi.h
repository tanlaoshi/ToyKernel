/*
 * SettingsUi.h — Settings 菜单（PR-D5 / PR-D7 / PR-G12）
 *
 * 一级：Desktop / Shell 背景、Font、Display（重启生效）、Language
 * 二级：具体颜色、字体或分辨率；数字键或鼠标点选，Esc/0 返回
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
int SettingsUiIsFocused(void);

#endif
