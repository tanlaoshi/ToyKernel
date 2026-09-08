/*
 * EditUi.h — 简易文本编辑器（PR-V2 / 1.3v）
 *
 * 内核窗：打开/编辑/保存 FAT 文本；教学级（非 IDE）。
 */
#ifndef EDIT_UI_H
#define EDIT_UI_H

#include "BootTypes.h"

/* Path 为 FileSystem 路径（可含 TOYOS:）；成功加载或空缓冲后由 GuiOpenEdit 调用 */
void EditUiOpen(const char *Path);
void EditUiRepaint(void);
void EditUiPaintFocused(void);
void EditUiOnClick(UINT32 X, UINT32 Y);
void EditUiOnEscape(void);
void EditUiOnEnter(void);
void EditUiOnArrow(int Down);
void EditUiOnArrowLeftRight(int Right);
void EditUiOnBackspace(void);
void EditUiOnDeleteKey(void);
void EditUiOnChar(char C);
void EditUiSave(void);
int EditUiIsFocused(void);

#endif
