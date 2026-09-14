/*
 * ToyGfx.h — 用户态绘图薄库（libToyGfx）
 *
 * ABI（PR-L3 / G-desk-3）：改签名 / 删符号须递增 TOY_GFX_ABI_VERSION_MAJOR。
 * 1.1.0：+ ToyGfxDamageRect（SYS_DAMAGE_RECT 像素 blit）。
 */
#ifndef TOY_GFX_H
#define TOY_GFX_H

#include <unistd.h>

#define TOY_GFX_ABI_VERSION_MAJOR 1
#define TOY_GFX_ABI_VERSION_MINOR 1
#define TOY_GFX_ABI_VERSION_PATCH 0
#define TOY_GFX_ABI_VERSION_STRING "1.1.0"

/* 0x00RRGGBB，与内核 UI 色值习惯一致 */
#define TOY_GFX_COLOR_BLACK       0x00000000u
#define TOY_GFX_COLOR_WHITE       0x00FFFFFFu
#define TOY_GFX_COLOR_LIGHT_GRAY  0x00C0C0C0u

/* 与内核 DAMAGE_RECT_MAX_PX 对齐：单次最多 64×64 */
#define TOY_GFX_DAMAGE_RECT_MAX_PIXELS 4096u

typedef struct {
    unsigned X;
    unsigned Y;
    unsigned W;
    unsigned H;
    const unsigned *Pixels; /* W*H × 0x00RRGGBB，行优先 */
} TOY_GFX_DAMAGE_RECT;

/*
 * ToyGfxDamageText — 在用户窗客户区显示一行文字
 * 成功 0；失败 -1（无效 wid / 空指针 / 内核拒绝）
 */
int ToyGfxDamageText(int WindowId, const char *Text);

/*
 * ToyGfxDamageRect — 客户区相对坐标像素矩形 blit（PR-G-desk-3）
 * 成功 0；失败 -1（越界 / 过大 / 空指针 / 内核拒绝）
 */
int ToyGfxDamageRect(int WindowId, const TOY_GFX_DAMAGE_RECT *Desc);

#endif
