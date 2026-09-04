/*
 * FilesUi.h — 文件浏览器（PR-FB1：只读列表 + 打开）
 *
 * 进目录；双击/Enter 打开 .ELF（ProcessExec）或文本预览；Esc 退出预览或返回上级。
 * 删除/新建见 PR-FB2。
 */
#ifndef FILES_UI_H
#define FILES_UI_H

#include "BootTypes.h"

void FilesUiOpen(void);
void FilesUiRepaint(void);
void FilesUiPaintFocused(void);
void FilesUiRefresh(void);
/* 客户区绝对坐标点击（含双击打开） */
void FilesUiOnClick(UINT32 X, UINT32 Y);
void FilesUiOnEscape(void);
void FilesUiOnEnter(void);
void FilesUiOnArrow(int Down);
int FilesUiIsFocused(void);

#endif
