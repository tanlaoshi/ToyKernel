/*
 * Font.h — 点阵字体抽象（PR-D1）
 *
 * 字形数据在 Common/Fonts/；绘制层（Video/Console）只经本 API，不直接 include 某份点阵表。
 * 加字体：Common/Fonts/ 新增数据 + FontRegistry 注册一行。
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

/* Terminus 16×32（Common/Fonts/terminus16x32.c） */
extern const FONT_FACE gFontFaceTerminus16x32;
/* 同字形 Scale=2（PR-D5） */
extern const FONT_FACE gFontFaceTerminusX2;
/* Terminus 10×18 独立点阵（PR-T2；Common/Fonts/terminus10x18.c） */
extern const FONT_FACE gFontFaceTerminus10x18;

void FontInitialize(void);
/*
 * PR-T3：FS 就绪后从 Assets/Fonts TOYF 追加运行时字面；缺文件回退内建。
 * 成功加载至少一包返回 0，否则 -1（仍可用内建）。
 */
int FontLoadAssets(void);
int FontReloadAssets(void);
/* PR-UI-ttf-0：读 CJK.TTF 校验 sfnt；不绘制。缺文件返回 -1 */
int FontTtfLoad(void);
const UINT8 *FontTtfBlob(UINT32 *OutSize);
int FontTtfInit(void);
int FontTtfRasterCp(UINT32 Cp, UINT8 *Pix18);
const UINT8 *FontTtfCacheGet(UINT32 Cp, UINT32 *OutW, UINT32 *OutH);
/* PR-UI-ttf-3：仅 Worker / 开窗短 burst；绘制路径 FontTtfCacheGet 只 Lookup */
void FontTtfPreheatUtf8(const char *S);
/* Worker：泵绘制路径排队的缺字；1=仍有排队 */
int FontTtfWantStep(void);
/* 开窗前/Worker：一次抽干最多 Max 个 Want（0→32） */
UINT32 FontTtfWantDrain(UINT32 Max);
/*
 * PR-S-app-font：按路径加载 TOYF 到专用应用槽（覆盖上次私有字）。
 * 成功返回字体 id（≥0）；失败 -1。不改当前选中 id。
 */
int FontLoadPath(const char *Path);
/* 卸应用私有槽；当前 id 若失效则钳到合法值 */
void FontUnloadApp(void);
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
 * 汉字等宽字形见 FontCjk32Lookup / FontGlyphCp。
 */
UINTN Utf8Decode(const char *S, UINT32 *OutCp);
/* ASCII→Terminus；汉字→CJK（默认 16×16×4bpp）；OutW/OutH 未乘 Scale */
const UINT8 *FontGlyphCp(UINT32 Cp, UINT32 *OutW, UINT32 *OutH);
/* 最近一次 FontGlyphCp 的位深（ASCII=1，TTF=8，cjk32=4） */
UINT32 FontGlyphCpBpp(void);
const UINT8 *FontCjk32Lookup(UINT32 Cp, UINT32 *OutW, UINT32 *OutH);
const UINT8 *FontCjk16Lookup(UINT32 Cp, UINT32 *OutW, UINT32 *OutH);
/* PR-UI-cjk-gray：CJK 点阵位深（1 或 4）；ASCII 仍 1bpp */
UINT32 FontCjkBitsPerPixel(void);
/* 当前 CJK 原生边长（与 gen --dim 一致） */
UINT32 FontCjkDim(void);
/* 仅 Face->Scale；不再为凑行高整数拉高字形 */
UINT32 FontGlyphStretch(UINT32 GlyphH);
/* UTF-8 字符串像素宽（含汉字前进） */
UINT32 FontCodepointAdvance(UINT32 Cp);
UINT32 FontStringWidth(const char *S);

#endif
