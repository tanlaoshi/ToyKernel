/*
 * Fonts/FontRegistry.c — 字体注册表与当前字体（PR-D1）
 */
#include "Font.h"

static const FONT_FACE *gFonts[] = {
    &gFontFaceTerminus16x32,
    &gFontFaceTerminusX2,
};

static UINT32 gCurrentId;

void FontInit(void) {
    gCurrentId = 0;
}

UINT32 FontCount(void) {
    return (UINT32)(sizeof(gFonts) / sizeof(gFonts[0]));
}

UINT32 FontCurrentId(void) {
    return gCurrentId;
}

const FONT_FACE *FontGetById(UINT32 Id) {
    if (Id >= FontCount()) {
        return 0;
    }
    return gFonts[Id];
}

const FONT_FACE *FontGetCurrent(void) {
    const FONT_FACE *F = FontGetById(gCurrentId);
    if (F == 0) {
        return &gFontFaceTerminus16x32;
    }
    return F;
}

int FontSetById(UINT32 Id) {
    if (Id >= FontCount()) {
        return -1;
    }
    gCurrentId = Id;
    return 0;
}

static UINT32 ScaleOf(const FONT_FACE *F) {
    return F->Scale ? F->Scale : 1u;
}

UINT32 FontCellW(void) {
    const FONT_FACE *F = FontGetCurrent();
    return F->Width * ScaleOf(F);
}

UINT32 FontCellH(void) {
    const FONT_FACE *F = FontGetCurrent();
    return F->Height * ScaleOf(F);
}

UINT32 FontAdvanceX(void) {
    const FONT_FACE *F = FontGetCurrent();
    UINT32 S = ScaleOf(F);
    return F->Width * S + F->CharSpacing * S;
}

UINT32 FontAdvanceY(void) {
    const FONT_FACE *F = FontGetCurrent();
    UINT32 S = ScaleOf(F);
    return F->Height * S + F->LineSpacing * S;
}

const UINT8 *FontGlyph(char C) {
    const FONT_FACE *F = FontGetCurrent();
    UINT32 U = (UINT32)(UINT8)C;

    if (F == 0 || F->Glyphs == 0 || F->BytesPerGlyph == 0) {
        return 0;
    }
    if (U < F->FirstChar || U >= F->FirstChar + F->GlyphCount) {
        return 0;
    }
    return F->Glyphs + (U - F->FirstChar) * F->BytesPerGlyph;
}
