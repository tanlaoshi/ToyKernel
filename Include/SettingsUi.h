/*
 * SettingsUi.h — Settings 文字菜单（PR-D5）
 *
 * 一级：Desktop / Shell 背景、Font
 * 二级：具体颜色或字体；数字键选择，Esc/0 返回
 */
#ifndef SETTINGS_UI_H
#define SETTINGS_UI_H

#include "BootTypes.h"

/* 打开 Settings 窗后调用：回到一级菜单并绘制 */
void SettingsUiOpen(void);
/* ThemeApply / GuiRedraw 后若 Settings 仍开着，重绘菜单 */
void SettingsUiRefresh(void);
/* 焦点在 Settings 时处理输入：Digit='0'..'9'；Esc 返回上级 */
void SettingsUiOnDigit(char Digit);
void SettingsUiOnEscape(void);
int SettingsUiIsFocused(void);

#endif
