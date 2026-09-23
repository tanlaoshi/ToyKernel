/*
 * VideoGlyph.c — 字符与点阵（PR-S-video-1）
 * PR-GUI-l2-font：1bpp 邻接覆盖做灰度边，不换字形格式。
 */
#include "VideoPrivate.h"

static int gGlyphSmooth;

void VideoSetGlyphSmooth(int On) {
    gGlyphSmooth = On ? 1 : 0;
}

static int GlyphBit(const UINT8 *Glyph, UINT32 Bpr, UINT32 Width, UINT32 Height,
                    INT32 Col, INT32 Row) {
    UINT8 Byte;
    int Bit;

    if (Col < 0 || Row < 0 || (UINT32)Col >= Width || (UINT32)Row >= Height) {
        return 0;
    }
    Byte = Glyph[(UINT32)Row * Bpr + ((UINT32)Col / 8u)];
    Bit = 7 - (int)((UINT32)Col % 8u);
    return (Byte & (1 << Bit)) ? 1 : 0;
}

/* 空像素贴着实心边时给一圈浅灰，台阶不再是硬切 */
static UINT8 EdgeAlpha(const UINT8 *Glyph, UINT32 Bpr, UINT32 Width, UINT32 Height,
                       UINT32 Col, UINT32 Row) {
    int N = 0;

    if (GlyphBit(Glyph, Bpr, Width, Height, (INT32)Col - 1, (INT32)Row)) {
        N++;
    }
    if (GlyphBit(Glyph, Bpr, Width, Height, (INT32)Col + 1, (INT32)Row)) {
        N++;
    }
    if (GlyphBit(Glyph, Bpr, Width, Height, (INT32)Col, (INT32)Row - 1)) {
        N++;
    }
    if (GlyphBit(Glyph, Bpr, Width, Height, (INT32)Col, (INT32)Row + 1)) {
        N++;
    }
    if (N <= 0) {
        return 0;
    }
    if (N == 1) {
        return 112;
    }
    if (N == 2) {
        return 72;
    }
    return 48;
}

static void PaintBlock(UINT32 X, UINT32 Y, UINT32 ScaleX, UINT32 ScaleY,
                       UINT32 Color, UINT8 Alpha) {
    UINT32 Sy;
    UINT32 Sx;

    for (Sy = 0; Sy < ScaleY; Sy++) {
        for (Sx = 0; Sx < ScaleX; Sx++) {
            if (Alpha == 255) {
                VideoDrawPixel(X + Sx, Y + Sy, Color);
            } else {
                VideoBlendPixel(X + Sx, Y + Sy, Color, Alpha);
            }
        }
    }
}

static void PaintGlyph(UINT32 X, UINT32 Y, const UINT8 *Glyph, UINT32 Width,
                       UINT32 Height, UINT32 Bpr, UINT32 ScaleX, UINT32 ScaleY,
                       UINT32 OffY, UINT32 Color) {
    UINT32 Row;
    UINT32 Col;

    if (!Glyph || Width == 0 || Height == 0 || Bpr == 0) {
        return;
    }
    if (ScaleX < 1) {
        ScaleX = 1;
    }
    if (ScaleY < 1) {
        ScaleY = 1;
    }
    for (Row = 0; Row < Height; Row++) {
        for (Col = 0; Col < Width; Col++) {
            UINT32 Dx = X + Col * ScaleX;
            UINT32 Dy = Y + OffY + Row * ScaleY;
            UINT8 Alpha;

            if (GlyphBit(Glyph, Bpr, Width, Height, (INT32)Col, (INT32)Row)) {
                PaintBlock(Dx, Dy, ScaleX, ScaleY, Color, 255);
                continue;
            }
            if (!gGlyphSmooth) {
                continue;
            }
            Alpha = EdgeAlpha(Glyph, Bpr, Width, Height, Col, Row);
            if (Alpha != 0) {
                PaintBlock(Dx, Dy, ScaleX, ScaleY, Color, Alpha);
            }
        }
    }
}

void VideoDrawCharAt(UINT32 X, UINT32 Y, char C, UINT32 Color) {
    const FONT_FACE *F;
    const UINT8 *Glyph;
    UINT32 Scale;

    F = FontGetCurrent();
    Glyph = FontGlyph(C);
    if (F == 0 || Glyph == 0) {
        return;
    }
    Scale = F->Scale ? F->Scale : 1u;
    PaintGlyph(X, Y, Glyph, F->Width, F->Height, F->BytesPerRow, Scale, Scale,
               0, Color);
}

/* 任意点阵：BytesPerRow = (Width+7)/8；CJK 短于行高时 PR-T1 拉伸至 FontCellH */
static void VideoDrawBitmapAt(UINT32 X, UINT32 Y, const UINT8 *Glyph,
                              UINT32 Width, UINT32 Height, UINT32 Color) {
    UINT32 ScaleX;
    UINT32 ScaleY;
    UINT32 CellH;
    UINT32 OffY;
    UINT32 DrawnH;

    if (!Glyph || Width == 0 || Height == 0) {
        return;
    }
    ScaleY = FontGlyphStretch(Height);
    ScaleX = ScaleY;
    if (ScaleX < 1) {
        ScaleX = 1;
    }
    if (ScaleY < 1) {
        ScaleY = 1;
    }
    CellH = FontCellH();
    DrawnH = Height * ScaleY;
    OffY = 0;
    if (CellH > DrawnH) {
        OffY = (CellH - DrawnH) / 2;
    }
    PaintGlyph(X, Y, Glyph, Width, Height, (Width + 7) / 8, ScaleX, ScaleY, OffY,
               Color);
}

void VideoDrawCodepointAt(UINT32 X, UINT32 Y, UINT32 Cp, UINT32 Color) {
    UINT32 W;
    UINT32 H;
    const UINT8 *G;

    if (Cp < 128) {
        VideoDrawCharAt(X, Y, (char)Cp, Color);
        return;
    }
    G = FontGlyphCp(Cp, &W, &H);
    if (!G) {
        return;
    }
    VideoDrawBitmapAt(X, Y, G, W, H, Color);
}

void VideoDrawStringAt(UINT32 X, UINT32 Y, const char *Text, UINT32 Color) {
    UINT32 CurX = X;
    UINT32 AdvY = FontAdvanceY();

    while (Text && *Text) {
        UINT32 Cp;
        UINTN N;

        if (*Text == '\n') {
            CurX = X;
            Y += AdvY;
            Text++;
            continue;
        }
        N = Utf8Decode(Text, &Cp);
        if (N == 0) {
            Text++;
            continue;
        }
        VideoDrawCodepointAt(CurX, Y, Cp, Color);
        CurX += CodepointAdvance(Cp);
        Text += N;
    }
}
