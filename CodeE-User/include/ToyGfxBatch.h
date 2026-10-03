/*
 * ToyGfxBatch.h — 用户态绘制命令缓冲（PR-GFX-batch-0）
 *
 * 一帧内收集绘制命令，ToyGfxBatchEnd 统一回放至既有 ToyGfx*（64×64 分块）。
 * 不改 ToyGfx.h ABI；新增本头 + 库。
 *
 * 用法：
 *   ToyGfxBatchBegin(Wid);
 *   ToyGfxBatchFillRect(...);
 *   ToyGfxBatchDrawRect(...);
 *   ToyGfxBatchDrawLine(...);
 *   ToyGfxBatchDrawPixel(...);
 *   ToyGfxBatchText("...");
 *   ToyGfxBatchEnd();   -- 统一回放
 */
#ifndef TOY_GFX_BATCH_H
#define TOY_GFX_BATCH_H

#define TOY_GFX_BATCH_CMD_MAX 64

typedef enum {
    TOY_GFX_BATCH_NONE = 0,
    TOY_GFX_BATCH_FILL,
    TOY_GFX_BATCH_RECT,
    TOY_GFX_BATCH_LINE,
    TOY_GFX_BATCH_PIXEL,
    TOY_GFX_BATCH_TEXT
} TOY_GFX_BATCH_KIND;

typedef struct {
    int Kind;
    int X0, Y0, X1, Y1;
    unsigned Color;
    char Text[20];
} TOY_GFX_BATCH_CMD;

int ToyGfxBatchBegin(int WindowId);
int ToyGfxBatchFillRect(unsigned X, unsigned Y, unsigned W, unsigned H,
                         unsigned Color);
int ToyGfxBatchDrawRect(unsigned X, unsigned Y, unsigned W, unsigned H,
                        unsigned Color);
int ToyGfxBatchDrawLine(int X0, int Y0, int X1, int Y1, unsigned Color);
int ToyGfxBatchDrawPixel(unsigned X, unsigned Y, unsigned Color);
int ToyGfxBatchText(const char *Text);
int ToyGfxBatchEnd(void);

#endif
