/*
 * SettingsUi.h — Settings 文字菜单（PR-D5 / PR-D7）
 *
 * 一级：Desktop / Shell 背景、Font、Display（重启生效）
 * 二级：具体颜色、字体或分辨率；数字键选择，Esc/0 返回
 */
#ifndef SETTINGS_UI_H
#define SETTINGS_UI_H

#include "BootTypes.h"

/* 打开 Settings 窗后调用：回到一级菜单并绘制 */
void SettingsUiOpen(void);
/* ThemeApply / GuiRedraw 后若 Settings 仍开着，重绘菜单 */
void SettingsUiRefresh(void);
/* 仅重绘菜单（不 Raise）；置顶后备份缺失时补内容 */
void SettingsUiRepaint(void);
/* PR-G8：焦点已是 Settings 时画菜单，绝不 Raise */
void SettingsUiPaintFocused(void);
/* 焦点在 Settings 时处理输入：Digit='0'..'9'；Esc 返回上级 */
void SettingsUiOnDigit(char Digit);
void SettingsUiOnEscape(void);
int SettingsUiIsFocused(void);

#endif
