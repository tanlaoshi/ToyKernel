/*
 * VideoGlyph.c — 字符与点阵（PR-S-video-1）
 */
#include "VideoPrivate.h"

void VideoDrawCharAt(UINT32 X, UINT32 Y, char C, UINT32 Color) {
    const FONT_FACE *F;
    const UINT8 *Glyph;
    UINT32 Scale;
    UINT32 Row;
    UINT32 Col;
    UINT32 Sy;
    UINT32 Sx;

    F = FontGetCurrent();
    Glyph = FontGlyph(C);
    if (F == 0 || Glyph == 0) {
        return;
    }
    Scale = F->Scale ? F->Scale : 1u;

    for (Row = 0; Row < F->Height; Row++) {
        for (Col = 0; Col < F->Width; Col++) {
            UINT8 Byte = Glyph[Row * F->BytesPerRow + (Col / 8)];
            int Bit = 7 - (int)(Col % 8);
            if ((Byte & (1 << Bit)) == 0) {
                continue;
            }
            for (Sy = 0; Sy < Scale; Sy++) {
                for (Sx = 0; Sx < Scale; Sx++) {
                    VideoDrawPixel(
                        X + Col * Scale + Sx,
                        Y + Row * Scale + Sy,
                        Color);
                }
            }
        }
    }
}

/* 任意点阵：BytesPerRow = (Width+7)/8；CJK 短于行高时 PR-T1 拉伸至 FontCellH */
static void VideoDrawBitmapAt(UINT32 X, UINT32 Y, const UINT8 *Glyph,
                              UINT32 Width, UINT32 Height, UINT32 Color) {
    UINT32 ScaleX;
    UINT32 ScaleY;
    UINT32 Bpr;
    UINT32 Row;
    UINT32 Col;
    UINT32 Sy;
    UINT32 Sx;
    UINT32 CellH;
    UINT32 OffY;
    UINT32 DrawnH;

    if (!Glyph || Width == 0 || Height == 0) {
        return;
    }
    ScaleY = FontGlyphStretch(Height);
    ScaleX = ScaleY; /* 方形拉伸；与英文同高 */
    if (ScaleX < 1) {
        ScaleX = 1;
    }
    if (ScaleY < 1) {
        ScaleY = 1;
    }
    CellH = FontCellH();
    DrawnH = Height * ScaleY;
    OffY = 0;
    /* 整数拉伸凑不满行高时（如 10×18 下 CJK 16）垂直居中 */
    if (CellH > DrawnH) {
        OffY = (CellH - DrawnH) / 2;
    }
    Bpr = (Width + 7) / 8;

    for (Row = 0; Row < Height; Row++) {
        for (Col = 0; Col < Width; Col++) {
            UINT8 Byte = Glyph[Row * Bpr + (Col / 8)];
            int Bit = 7 - (int)(Col % 8);
            if ((Byte & (1 << Bit)) == 0) {
                continue;
            }
            for (Sy = 0; Sy < ScaleY; Sy++) {
                for (Sx = 0; Sx < ScaleX; Sx++) {
                    VideoDrawPixel(
                        X + Col * ScaleX + Sx,
                        Y + OffY + Row * ScaleY + Sy,
                        Color);
                }
            }
        }
    }
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
