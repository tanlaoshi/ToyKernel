/*
 * Bmp.h — 未压缩 BMP 解码（PR-G13 壁纸）
 *
 * 仅支持 BI_RGB 24/32 bpp、正高度或负高度（top-down）。
 * 输出 0x00RRGGBB 像素缓冲，由调用方 BmpFree / PhysicalMemoryFreePages。
 */
#ifndef BMP_H
#define BMP_H

#include "BootTypes.h"

typedef struct {
    UINT32 *Pixels; /* 行主序，上→下 */
    UINT32  Width;
    UINT32  Height;
    UINT32  Pages;  /* 分配页数，供释放 */
} BMP_IMAGE;

/* 成功 0；失败非 0（不写有效 Pixels） */
int BmpDecode(const void *Data, UINTN Size, BMP_IMAGE *Out);
void BmpFree(BMP_IMAGE *Img);

#endif
