/*
 * ToyGfx.h — 用户态绘图薄库（libToyGfx）
 *
 * ABI（PR-L3）：改签名 / 删符号须递增 TOY_GFX_ABI_VERSION_MAJOR。
 * 当前后端：G14 SYS_DAMAGE 文字；像素 blit 后置，不进本 ABI。
 */
#ifndef TOY_GFX_H
#define TOY_GFX_H

#include <unistd.h>

#define TOY_GFX_ABI_VERSION_MAJOR 1
#define TOY_GFX_ABI_VERSION_MINOR 0
#define TOY_GFX_ABI_VERSION_PATCH 0
#define TOY_GFX_ABI_VERSION_STRING "1.0.0"

/* 0x00RRGGBB，与内核 UI 色值习惯一致（本版未用于像素绘制） */
#define TOY_GFX_COLOR_BLACK       0x00000000u
#define TOY_GFX_COLOR_WHITE       0x00FFFFFFu
#define TOY_GFX_COLOR_LIGHT_GRAY  0x00C0C0C0u

/*
 * ToyGfxDamageText — 在用户窗客户区显示一行文字
 * 成功 0；失败 -1（无效 wid / 空指针 / 内核拒绝）
 */
int ToyGfxDamageText(int WindowId, const char *Text);

#endif
