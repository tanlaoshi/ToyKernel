/*
 * ToyGfx.h — 用户态绘图薄库（PR-G15 / libToyGfx）
 * 命名：PascalCase 模块名；产物链为 libToyGfx.a（非 libgfx）。
 * 当前后端走 G14 窗口协议（文字 damage）；像素 blit 后置。
 */
#ifndef TOY_GFX_H
#define TOY_GFX_H

#include "unistd.h"

/* 0x00RRGGBB，与内核 UI.h 一致，供 demo 文档对照 */
#define TOY_GFX_COLOR_BLACK       0x00000000
#define TOY_GFX_COLOR_WHITE       0x00FFFFFF
#define TOY_GFX_COLOR_LIGHT_GRAY  0x00C0C0C0

/* 在用户窗客户区显示一行文字（SYS_DAMAGE） */
int ToyGfxDamageText(int WindowId, const char *Text);

#endif
