/*
 * Video.c — GOP 帧缓冲驱动（PR-G9：可选 backbuffer + 脏矩形 Present）
 *
 * 绘制写入后缓冲（若已启用）；VideoPresent 将脏区一次 blit 到 scanout。
 * 字形经 Font_*（Fonts/），不直接绑定某一份点阵表。
 */
#include "Video.h"
#include "Font.h"

static SCREEN_INFO gScreen = {0};
static UINT32 gBackground = 0x00000000;
static int gClipOn;
static UINT32 gClipX;
static UINT32 gClipY;
static UINT32 gClipW;
static UINT32 gClipH;
static UINT32 gClipBg;

/* scanout（GOP）与后缓冲 */
static UINT32 *gFront;
static UINT32  gFrontPitch;
static UINT32 *gBack;
static UINT32  gBackPitch;
static UINT32  gBackPages;
static int     gBackOn;

/* 脏矩形 [gDx0,gDx1) x [gDy0,gDy1) */
static int     gDirty;
static UINT32  gDx0;
static UINT32  gDy0;
static UINT32  gDx1;
static UINT32  gDy1;

static void DirtyUnion(UINT32 X, UINT32 Y, UINT32 W, UINT32 H) {
    UINT32 X1;
    UINT32 Y1;

    if (!W || !H || gScreen.Width == 0 || gScreen.Height == 0) {
        return;
    }
    if (X >= gScreen.Width || Y >= gScreen.Height) {
        return;
    }
    X1 = X + W;
    Y1 = Y + H;
    if (X1 > gScreen.Width) {
        X1 = gScreen.Width;
    }
    if (Y1 > gScreen.Height) {
        Y1 = gScreen.Height;
    }
    if (X >= X1 || Y >= Y1) {
        return;
    }
    if (!gDirty) {
        gDx0 = X;
        gDy0 = Y;
        gDx1 = X1;
        gDy1 = Y1;
        gDirty = 1;
        return;
    }
    if (X < gDx0) {
        gDx0 = X;
    }
    if (Y < gDy0) {
        gDy0 = Y;
    }
    if (X1 > gDx1) {
        gDx1 = X1;
    }
    if (Y1 > gDy1) {
        gDy1 = Y1;
    }
}

static UINT32 *DrawBase(void) {
    return gBackOn ? gBack : gFront;
}

static UINT32 DrawPitch(void) {
    return gBackOn ? gBackPitch : gFrontPitch;
}

void VideoSet(VIDEO_CONFIG *VideoConfig) {
    gScreen.Width = VideoConfig->HorizontalResolution;
    gScreen.Height = VideoConfig->VerticalResolution;
    gScreen.PixelsPerScanLine = VideoConfig->PixelsPerScanLine;
    gScreen.FrameBufferBase = VideoConfig->FrameBufferBase;
    gScreen.FrameBufferSize = VideoConfig->FrameBufferSize;
    gScreen.CursorX = 0;
    gScreen.CursorY = 0;
    gFront = (UINT32 *)(UINTN)VideoConfig->FrameBufferBase;
    gFrontPitch = VideoConfig->PixelsPerScanLine;
    gBack = 0;
    gBackPitch = 0;
    gBackPages = 0;
    gBackOn = 0;
    gDirty = 0;
}

/*
 * 启用与屏同尺寸的后缓冲（紧密 pitch=Width）。Buf 由调用方 PMM 分配。
 * Pages 仅记录；失败/空指针则保持直写 GOP。
 */
void VideoSetBackbuffer(UINT32 *Buf, UINT32 Pages) {
    if (!Buf || gScreen.Width == 0 || gScreen.Height == 0 || !gFront) {
        gBack = 0;
        gBackPages = 0;
        gBackOn = 0;
        return;
    }
    gBack = Buf;
    gBackPitch = gScreen.Width;
    gBackPages = Pages;
    gBackOn = 1;
    /*
     * 不从 GOP 全屏拷：InitVideo 紧接着 ClearScreen+Present。
     * 后缓冲内容以后续绘制为准。
     */
    gDirty = 0;
}

int VideoBackbufferEnabled(void) {
    return gBackOn;
}

UINT32 VideoBackbufferPages(void) {
    return gBackPages;
}

/* 将脏区（或全屏若从未标记）blit 到 GOP；无后缓冲时为空操作 */
void VideoPresent(void) {
    UINT32 Y;
    UINT32 X;
    UINT32 X0;
    UINT32 Y0;
    UINT32 X1;
    UINT32 Y1;

    if (!gBackOn || !gBack || !gFront) {
        gDirty = 0;
        return;
    }
    if (!gDirty) {
        return;
    }
    X0 = gDx0;
    Y0 = gDy0;
    X1 = gDx1;
    Y1 = gDy1;
    if (X1 > gScreen.Width) {
        X1 = gScreen.Width;
    }
    if (Y1 > gScreen.Height) {
        Y1 = gScreen.Height;
    }
    for (Y = Y0; Y < Y1; Y++) {
        UINT32 *Src = &gBack[Y * gBackPitch + X0];
        UINT32 *Dst = &gFront[Y * gFrontPitch + X0];

        for (X = X0; X < X1; X++) {
            *Dst++ = *Src++;
        }
    }
    gDirty = 0;
}

void VideoGetSize(UINT32 *Width, UINT32 *Height) {
    if (Width) {
        *Width = gScreen.Width;
    }
    if (Height) {
        *Height = gScreen.Height;
    }
}

void VideoSetClipRegion(UINT32 X, UINT32 Y, UINT32 Width, UINT32 Height, UINT32 Background) {
    gClipOn = 1;
    gClipX = X;
    gClipY = Y;
    gClipW = Width;
    gClipH = Height;
    gClipBg = Background;
    gBackground = Background;
}

void VideoSetClipOrigin(UINT32 X, UINT32 Y, UINT32 Width, UINT32 Height, UINT32 Background) {
    VideoSetClipRegion(X, Y, Width, Height, Background);
    gScreen.CursorX = X;
    gScreen.CursorY = Y;
}

void VideoGetTextCursor(UINT32 *X, UINT32 *Y) {
    if (X) {
        *X = gScreen.CursorX;
    }
    if (Y) {
        *Y = gScreen.CursorY;
    }
}

void VideoSetTextCursor(UINT32 X, UINT32 Y) {
    gScreen.CursorX = X;
    gScreen.CursorY = Y;
}

void VideoClearClip(void) {
    gClipOn = 0;
}

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

/* 任意点阵：BytesPerRow = (Width+7)/8；Scale 取当前字体 */
static void VideoDrawBitmapAt(UINT32 X, UINT32 Y, const UINT8 *Glyph,
                              UINT32 Width, UINT32 Height, UINT32 Color) {
    const FONT_FACE *F;
    UINT32 Scale;
    UINT32 Bpr;
    UINT32 Row;
    UINT32 Col;
    UINT32 Sy;
    UINT32 Sx;
    UINT32 CellH;
    UINT32 OffY;

    if (!Glyph || Width == 0 || Height == 0) {
        return;
    }
    F = FontGetCurrent();
    Scale = (F && F->Scale) ? F->Scale : 1u;
    Bpr = (Width + 7) / 8;
    CellH = FontCellH();
    OffY = 0;
    if (CellH > Height * Scale) {
        OffY = (CellH - Height * Scale) / 2;
    }

    for (Row = 0; Row < Height; Row++) {
        for (Col = 0; Col < Width; Col++) {
            UINT8 Byte = Glyph[Row * Bpr + (Col / 8)];
            int Bit = 7 - (int)(Col % 8);
            if ((Byte & (1 << Bit)) == 0) {
                continue;
            }
            for (Sy = 0; Sy < Scale; Sy++) {
                for (Sx = 0; Sx < Scale; Sx++) {
                    VideoDrawPixel(
                        X + Col * Scale + Sx,
                        Y + OffY + Row * Scale + Sy,
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

static UINT32 CodepointAdvance(UINT32 Cp) {
    return FontCodepointAdvance(Cp);
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

void VideoDrawPixelRaw(UINT32 X, UINT32 Y, UINT32 Color) {
    UINT32 *Fb;
    UINT32 Pitch;

    if (X >= gScreen.Width || Y >= gScreen.Height) {
        return;
    }
    Fb = DrawBase();
    Pitch = DrawPitch();
    if (!Fb || Pitch == 0) {
        return;
    }
    Fb[Y * Pitch + X] = Color;
    DirtyUnion(X, Y, 1, 1);
}

void VideoDrawPixel(UINT32 X, UINT32 Y, UINT32 Color) {
    if (gClipOn) {
        if (X < gClipX || Y < gClipY ||
            X >= gClipX + gClipW || Y >= gClipY + gClipH) {
            return;
        }
    }
    VideoDrawPixelRaw(X, Y, Color);
}

UINT32 VideoReadPixel(UINT32 X, UINT32 Y) {
    UINT32 *Fb;
    UINT32 Pitch;

    if (X >= gScreen.Width || Y >= gScreen.Height) {
        return 0;
    }
    Fb = DrawBase();
    Pitch = DrawPitch();
    if (!Fb || Pitch == 0) {
        return 0;
    }
    return Fb[Y * Pitch + X];
}

void VideoFillRect(UINT32 X, UINT32 Y, UINT32 Width, UINT32 Height, UINT32 Color) {
    UINT32 *Fb;
    UINT32 Pitch;
    UINT32 Row;
    UINT32 Col;
    UINT32 CopyW;
    UINT32 CopyH;
    UINT32 X0;
    UINT32 Y0;

    if (!Width || !Height) {
        return;
    }
    Fb = DrawBase();
    Pitch = DrawPitch();
    if (!Fb || Pitch == 0) {
        return;
    }
    X0 = X;
    Y0 = Y;
    CopyW = Width;
    CopyH = Height;

    /* PR-G10 M5：与 DrawPixel 一致，尊重客户区 clip */
    if (gClipOn) {
        UINT32 ClipR = gClipX + gClipW;
        UINT32 ClipB = gClipY + gClipH;
        UINT32 R;
        UINT32 B;

        if (X0 >= ClipR || Y0 >= ClipB) {
            return;
        }
        if (X0 < gClipX) {
            UINT32 Skip = gClipX - X0;
            if (Skip >= CopyW) {
                return;
            }
            CopyW -= Skip;
            X0 = gClipX;
        }
        if (Y0 < gClipY) {
            UINT32 Skip = gClipY - Y0;
            if (Skip >= CopyH) {
                return;
            }
            CopyH -= Skip;
            Y0 = gClipY;
        }
        if (CopyW == 0 || CopyH == 0) {
            return;
        }
        R = X0 + CopyW;
        B = Y0 + CopyH;
        if (R > ClipR) {
            CopyW = ClipR - X0;
        }
        if (B > ClipB) {
            CopyH = ClipB - Y0;
        }
    }

    if (X0 >= gScreen.Width || Y0 >= gScreen.Height) {
        return;
    }
    if (X0 + CopyW > gScreen.Width) {
        CopyW = gScreen.Width - X0;
    }
    if (Y0 + CopyH > gScreen.Height) {
        CopyH = gScreen.Height - Y0;
    }
    if (CopyW == 0 || CopyH == 0) {
        return;
    }
    for (Row = 0; Row < CopyH; Row++) {
        UINT32 *Line = &Fb[(Y0 + Row) * Pitch + X0];

        for (Col = 0; Col < CopyW; Col++) {
            Line[Col] = Color;
        }
    }
    DirtyUnion(X0, Y0, CopyW, CopyH);
}

void VideoCopyRect(UINT32 SrcX, UINT32 SrcY, UINT32 DstX, UINT32 DstY,
                   UINT32 Width, UINT32 Height) {
    UINT32 *Fb = DrawBase();
    UINT32 Pitch = DrawPitch();
    INT32 Y;
    INT32 X;
    INT32 W;
    INT32 H;
    UINT32 CopyW = Width;
    UINT32 CopyH = Height;

    if (!Fb || !Width || !Height || Pitch == 0) {
        return;
    }
    if (SrcX >= gScreen.Width || SrcY >= gScreen.Height ||
        DstX >= gScreen.Width || DstY >= gScreen.Height) {
        return;
    }
    if (SrcX + CopyW > gScreen.Width) {
        CopyW = gScreen.Width - SrcX;
    }
    if (DstX + CopyW > gScreen.Width) {
        CopyW = gScreen.Width - DstX;
    }
    if (SrcY + CopyH > gScreen.Height) {
        CopyH = gScreen.Height - SrcY;
    }
    if (DstY + CopyH > gScreen.Height) {
        CopyH = gScreen.Height - DstY;
    }
    W = (INT32)CopyW;
    H = (INT32)CopyH;
    if (W <= 0 || H <= 0) {
        return;
    }

    /* PR-G10 L5：按行拷贝（同向/逆向处理重叠） */
    if (DstY > SrcY || (DstY == SrcY && DstX > SrcX)) {
        for (Y = H - 1; Y >= 0; Y--) {
            UINT32 *Dst = &Fb[(DstY + (UINT32)Y) * Pitch + DstX];
            UINT32 *Src = &Fb[(SrcY + (UINT32)Y) * Pitch + SrcX];
            for (X = W - 1; X >= 0; X--) {
                Dst[X] = Src[X];
            }
        }
    } else if (DstY != SrcY || DstX != SrcX) {
        for (Y = 0; Y < H; Y++) {
            UINT32 *Dst = &Fb[(DstY + (UINT32)Y) * Pitch + DstX];
            UINT32 *Src = &Fb[(SrcY + (UINT32)Y) * Pitch + SrcX];
            for (X = 0; X < W; X++) {
                Dst[X] = Src[X];
            }
        }
    }
    DirtyUnion(DstX, DstY, (UINT32)W, (UINT32)H);
}

void VideoReadRect(UINT32 X, UINT32 Y, UINT32 Width, UINT32 Height, UINT32 *Out) {
    UINT32 *Fb = DrawBase();
    UINT32 Pitch = DrawPitch();
    UINT32 Row;
    UINT32 Col;
    UINT32 i = 0;

    if (!Fb || !Out || !Width || !Height || Pitch == 0) {
        return;
    }
    if (X >= gScreen.Width || Y >= gScreen.Height) {
        return;
    }
    if (X + Width > gScreen.Width) {
        Width = gScreen.Width - X;
    }
    if (Y + Height > gScreen.Height) {
        Height = gScreen.Height - Y;
    }
    for (Row = 0; Row < Height; Row++) {
        for (Col = 0; Col < Width; Col++) {
            Out[i++] = Fb[(Y + Row) * Pitch + X + Col];
        }
    }
}

void VideoWriteRect(UINT32 X, UINT32 Y, UINT32 Width, UINT32 Height, const UINT32 *In) {
    UINT32 *Fb = DrawBase();
    UINT32 Pitch = DrawPitch();
    UINT32 Row;
    UINT32 SrcStride;
    UINT32 CopyW;
    UINT32 CopyH;

    if (!Fb || !In || !Width || !Height || Pitch == 0) {
        return;
    }
    if (X >= gScreen.Width || Y >= gScreen.Height) {
        return;
    }
    SrcStride = Width;
    CopyW = Width;
    CopyH = Height;
    if (X + CopyW > gScreen.Width) {
        CopyW = gScreen.Width - X;
    }
    if (Y + CopyH > gScreen.Height) {
        CopyH = gScreen.Height - Y;
    }
    for (Row = 0; Row < CopyH; Row++) {
        UINT32 *Dst = &Fb[(Y + Row) * Pitch + X];
        const UINT32 *Src = &In[Row * SrcStride];
        UINT32 Col;

        for (Col = 0; Col < CopyW; Col++) {
            Dst[Col] = Src[Col];
        }
    }
    DirtyUnion(X, Y, CopyW, CopyH);
}

void VideoClearScreen(UINT32 Color) {
    if (!gFront && !gBack) {
        return;
    }
    VideoFillRect(0, 0, gScreen.Width, gScreen.Height, Color);
    gScreen.CursorX = 0;
    gScreen.CursorY = 0;
    gBackground = Color;
}

static void ScrollClip(void) {
    UINT32 *Fb = DrawBase();
    UINT32 Pitch = DrawPitch();
    UINT32 LineHeight = FontAdvanceY();
    UINT32 Y;
    UINT32 X;
    UINT32 XEnd;
    UINT32 YEnd;

    if (!Fb || Pitch == 0 || gClipW == 0 || gClipH <= LineHeight) {
        return;
    }
    XEnd = gClipX + gClipW;
    YEnd = gClipY + gClipH;
    for (Y = gClipY; Y + LineHeight < YEnd; Y++) {
        for (X = gClipX; X < XEnd; X++) {
            Fb[Y * Pitch + X] = Fb[(Y + LineHeight) * Pitch + X];
        }
    }
    for (Y = YEnd - LineHeight; Y < YEnd; Y++) {
        for (X = gClipX; X < XEnd; X++) {
            Fb[Y * Pitch + X] = gClipBg;
        }
    }
    DirtyUnion(gClipX, gClipY, gClipW, gClipH);
}

static void ScrollScreen(void) {
    UINT32 *Fb = DrawBase();
    UINT32 Pitch = DrawPitch();
    UINT32 LineHeight;
    UINT32 Y;
    UINT32 X;
    UINT32 W;

    if (!Fb || Pitch == 0) {
        return;
    }
    if (gClipOn) {
        ScrollClip();
        return;
    }

    LineHeight = FontAdvanceY();
    W = gScreen.Width;
    for (Y = 0; Y < gScreen.Height - LineHeight; Y++) {
        for (X = 0; X < W; X++) {
            Fb[Y * Pitch + X] = Fb[(Y + LineHeight) * Pitch + X];
        }
    }
    for (Y = gScreen.Height - LineHeight; Y < gScreen.Height; Y++) {
        for (X = 0; X < W; X++) {
            Fb[Y * Pitch + X] = gBackground;
        }
    }
    DirtyUnion(0, 0, gScreen.Width, gScreen.Height);
}

void VideoNewLine(void) {
    UINT32 LineHeight = FontAdvanceY();

    if ((!gFront && !gBack) || gScreen.Height == 0 || gScreen.Width == 0) {
        return;
    }

    if (gClipOn) {
        if (gClipH <= LineHeight) {
            gScreen.CursorX = gClipX;
            return;
        }
        gScreen.CursorX = gClipX;
        gScreen.CursorY += LineHeight;
        while (gScreen.CursorY + FontCellH() > gClipY + gClipH) {
            ScrollClip();
            if (gScreen.CursorY < LineHeight) {
                gScreen.CursorY = gClipY;
                break;
            }
            gScreen.CursorY -= LineHeight;
        }
        return;
    }

    gScreen.CursorX = 0;
    gScreen.CursorY += LineHeight;

    while (gScreen.CursorY + FontCellH() > gScreen.Height) {
        ScrollScreen();
        if (gScreen.CursorY < LineHeight) {
            gScreen.CursorY = 0;
            break;
        }
        gScreen.CursorY -= LineHeight;
    }
}

void VideoDrawChar(char c, UINT32 Color) {
    if ((UINT8)c < 32 || (UINT8)c > 126) {
        return;
    }
    VideoDrawCodepoint(c, Color);
}

void VideoDrawCodepoint(UINT32 Cp, UINT32 Color) {
    UINT32 MaxX;
    UINT32 MaxY;
    UINT32 Adv;

    if (Cp == 0 || Cp == '\n') {
        return;
    }
    if ((!gFront && !gBack) || gScreen.Width == 0 || gScreen.Height == 0) {
        return;
    }
    if (Cp < 128 && (Cp < 32 || Cp > 126)) {
        return;
    }
    if (Cp >= 128 && !FontGlyphCp(Cp, (UINT32 *)0, (UINT32 *)0)) {
        return;
    }

    if (gClipOn) {
        MaxX = gClipX + gClipW;
        MaxY = gClipY + gClipH;
        if (gScreen.CursorX < gClipX) {
            gScreen.CursorX = gClipX;
        }
        if (gScreen.CursorY < gClipY) {
            gScreen.CursorY = gClipY;
        }
    } else {
        MaxX = gScreen.Width;
        MaxY = gScreen.Height;
    }

    Adv = CodepointAdvance(Cp);
    if (gScreen.CursorX + Adv > MaxX) {
        VideoNewLine();
    }

    if (gScreen.CursorY + FontCellH() > MaxY) {
        VideoNewLine();
    }

    VideoDrawCodepointAt(gScreen.CursorX, gScreen.CursorY, Cp, Color);
    gScreen.CursorX += Adv;
}

void VideoEraseLastChar(void) {
    UINT32 Step = FontAdvanceX();
    UINT32 MinX = gClipOn ? gClipX : 0;

    if (gScreen.CursorX < MinX + Step) {
        return;
    }
    gScreen.CursorX -= Step;
    for (UINT32 Row = 0; Row < FontCellH(); Row++) {
        for (UINT32 Col = 0; Col < Step; Col++) {
            VideoDrawPixel(gScreen.CursorX + Col, gScreen.CursorY + Row, gBackground);
        }
    }
}

void VideoDrawString(const char *Text, UINT32 Color) {
    while (Text && *Text) {
        UINT32 Cp;
        UINTN N;

        if (*Text == '\n') {
            VideoNewLine();
            Text++;
            continue;
        }
        N = Utf8Decode(Text, &Cp);
        if (N == 0) {
            Text++;
            continue;
        }
        VideoDrawCodepoint(Cp, Color);
        Text += N;
    }
}
