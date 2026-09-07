/*
 * FilesUi.h — 文件浏览器（PR-FB1/FB2 + PR-U1/U2/U3）
 *
 * 进目录；打开 .ELF / 文本预览；删除（确认）、mkdir、新建空文件、重命名。
 * U1：左栏固定宽；U2：侧栏书签；U3：右栏列表|预览分区。
 */
#ifndef FILES_UI_H
#define FILES_UI_H

#include "BootTypes.h"

void FilesUiOpen(void);
void FilesUiRepaint(void);
void FilesUiPaintFocused(void);
void FilesUiRefresh(void);
void FilesUiOnClick(UINT32 X, UINT32 Y);
/* PR-G11：指针在列表区移动时更新悬停行（可选对比，非新控件） */
void FilesUiOnHover(UINT32 X, UINT32 Y);
/* PR-I2：列表滚轮；非列表模式忽略 */
void FilesUiOnWheel(INT8 Wheel);
void FilesUiOnEscape(void);
void FilesUiOnEnter(void);
void FilesUiOnArrow(int Down);
void FilesUiOnBackspace(void);
void FilesUiOnChar(char C);
void FilesUiOnDeleteKey(void);
int FilesUiIsFocused(void);

#endif
