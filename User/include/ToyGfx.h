/*
 * ToyGfx.h — 用户态绘图薄库（libToyGfx）
 *
 * ABI：改签名 / 删符号须递增 TOY_GFX_ABI_VERSION_MAJOR。
 * 1.1.0：+ ToyGfxDamageRect（SYS_DAMAGE_RECT 像素 blit）。
 * 1.2.0：+ 点 / 线 / 填充矩形 / 矩形框（底层仍 DamageRect；单次 ≤64×64）。
 * 1.3.0：+ ToyGfxInitialize / ToyGfxSystemFontRoot（方案 C：系统字体约定）。
 */
#ifndef TOY_GFX_H
#define TOY_GFX_H

#include <unistd.h>

#define TOY_GFX_ABI_VERSION_MAJOR 1
#define TOY_GFX_ABI_VERSION_MINOR 3
#define TOY_GFX_ABI_VERSION_PATCH 0
#define TOY_GFX_ABI_VERSION_STRING "1.3.0"

/* 0x00RRGGBB，与内核 UI 色值习惯一致 */
#define TOY_GFX_COLOR_BLACK       0x00000000u
#define TOY_GFX_COLOR_WHITE       0x00FFFFFFu
#define TOY_GFX_COLOR_LIGHT_GRAY  0x00C0C0C0u

/* 与内核 DAMAGE_RECT_MAX_PX 对齐：单次最多 64×64 */
#define TOY_GFX_DAMAGE_RECT_MAX_PIXELS 4096u

/* 系统共享字库根（TOYOS 卷相对路径；应用勿拼私有路径） */
#define TOY_GFX_SYSTEM_FONT_ROOT "Assets/Fonts"

typedef struct {
    unsigned X;
    unsigned Y;
    unsigned W;
    unsigned H;
    const unsigned *Pixels; /* W*H × 0x00RRGGBB，行优先 */
} TOY_GFX_DAMAGE_RECT;

/*
 * ToyGfxInitialize — 声明使用系统字体（PR-S-bundle-font）
 *
 * 内核启动已加载 Assets/Fonts 下 .FNT；本调用无加载动作，成功恒 0。
 * 课上应在首绘 / DamageText 前调用，避免误以为要 FontLoad("...")。
 */
int ToyGfxInitialize(void);

/* 返回系统字库根字符串（静态常量，勿 free） */
const char *ToyGfxSystemFontRoot(void);

/*
 * ToyGfxDamageText — 在用户窗客户区显示一行文字（内核当前选中的系统字体）
 * 成功 0；失败 -1（无效 wid / 空指针 / 内核拒绝）
 */
int ToyGfxDamageText(int WindowId, const char *Text);

/*
 * ToyGfxDamageRect — 客户区相对坐标像素矩形 blit（PR-G-desk-3）
 * 成功 0；失败 -1（越界 / 过大 / 空指针 / 内核拒绝）
 */
int ToyGfxDamageRect(int WindowId, const TOY_GFX_DAMAGE_RECT *Desc);

/* PR-A-gfx-api：点 / 线 / 矩形。大矩形按 ≤64×64 分块。成功 0，失败 -1。 */
int ToyGfxDrawPixel(int WindowId, unsigned X, unsigned Y, unsigned Color);
int ToyGfxDrawLine(int WindowId, int X0, int Y0, int X1, int Y1, unsigned Color);
int ToyGfxFillRect(int WindowId, unsigned X, unsigned Y, unsigned W, unsigned H,
                   unsigned Color);
int ToyGfxDrawRect(int WindowId, unsigned X, unsigned Y, unsigned W, unsigned H,
                   unsigned Color);

#endif
