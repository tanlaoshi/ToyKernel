/*
 * Font.h — 点阵字体抽象（PR-D1）
 *
 * 字形数据在 Fonts/；绘制层（Video/Console）只经本 API，不直接 include 某份点阵表。
 * 加字体：Fonts/ 新增数据 + FontRegistry 注册一行。
 */
#ifndef FONT_H
#define FONT_H

#include "BootTypes.h"

typedef struct FONT_FACE {
    const char *Name;
    UINT32      Width;          /* 字形像素宽 */
    UINT32      Height;         /* 字形像素高 */
    UINT32      BytesPerGlyph;
    UINT32      BytesPerRow;
    UINT32      CharSpacing;    /* 字间距（逻辑像素，再乘 Scale） */
    UINT32      LineSpacing;    /* 行间距 */
    UINT32      Scale;          /* 像素放大倍数，至少 1 */
    const UINT8 *Glyphs;        /* FirstChar 起连续 GlyphCount 个字形 */
    UINT32      GlyphCount;
    UINT32      FirstChar;      /* 通常 32 */
} FONT_FACE;

/* Terminus 16×32（Fonts/terminus16x32.c） */
extern const FONT_FACE gFontFaceTerminus16x32;
/* 同字形 Scale=2（PR-D5） */
extern const FONT_FACE gFontFaceTerminusX2;

void FontInit(void);
UINT32 FontCount(void);
UINT32 FontCurrentId(void);
const FONT_FACE *FontGetById(UINT32 Id);
const FONT_FACE *FontGetCurrent(void);
/* 成功返回 0；Id 越界返回 -1 且保持当前字体 */
int FontSetById(UINT32 Id);

/* 当前字体度量（Scale 已计入） */
UINT32 FontCellW(void);
UINT32 FontCellH(void);
UINT32 FontAdvanceX(void);
UINT32 FontAdvanceY(void);
/* 可打印 ASCII 返回字形指针；否则 NULL */
const UINT8 *FontGlyph(char C);

/*
 * UTF-8：解析一个码点，返回消费字节数；非法序列返回 0。
 * 汉字等宽字形见 FontCjk16Lookup / FontGlyphCp。
 */
UINTN Utf8Decode(const char *S, UINT32 *OutCp);
/* ASCII→Terminus；基本汉字→CJK16；OutW/OutH 为点阵像素（未乘 Scale） */
const UINT8 *FontGlyphCp(UINT32 Cp, UINT32 *OutW, UINT32 *OutH);
const UINT8 *FontCjk16Lookup(UINT32 Cp, UINT32 *OutW, UINT32 *OutH);

#endif
