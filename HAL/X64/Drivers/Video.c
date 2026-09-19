/*
 * Video.c — GOP 帧缓冲驱动（PR-G9：可选 backbuffer + 脏矩形 Present）
 *
 * 绘制写入后缓冲（若已启用）；VideoPresent 将脏区一次 blit 到 scanout。
 * PR-G-present：脏区按行 memcpy（非整段逐像素）。
 * 字形经 Font_*（Common/Fonts/），不直接绑定某一份点阵表。
 */
#include "VideoPrivate.h"

extern void *memcpy(void *Dst, const void *Src, UINTN Len);
extern void *memmove(void *Dst, const void *Src, UINTN Len);

/* 全局定义集中在宿主；其它 TU 经 VideoPrivate.h extern */
SCREEN_INFO gScreen = {0};
UINT32 gBackground = 0x00000000;
int gClipOn;
UINT32 gClipX;
UINT32 gClipY;
UINT32 gClipW;
UINT32 gClipH;
UINT32 gClipBg;

/* scanout（GOP）与后缓冲 */
UINT32 *gFront;
UINT32  gFrontPitch;
UINT32 *gBack;
UINT32  gBackPitch;
UINT32  gBackPages;
int     gBackOn;
/* 真机 boot mark：直写 scanout，避开后缓冲 Present 假死 */
int     gForceFront;
/* UI 缩放：逻辑坐标画后缓冲，Present 最近邻贴到物理 GOP（50/100/150/200） */
UINT32  gPhysW;
UINT32  gPhysH;
UINT32  gUiScale = 100;

/* 内容脏矩形 [gDx0,gDx1) x [gDy0,gDy1)；光标 XOR 单独跟踪，避免 AABB 并成近全屏 */
int     gDirty;
UINT32  gDx0;
UINT32  gDy0;
UINT32  gDx1;
UINT32  gDy1;
int     gCurDirty;
UINT32  gCx0;
UINT32  gCy0;
UINT32  gCx1;
UINT32  gCy1;
/* 光标叠层绘制：DirtyUnion 改记光标矩形 */
int     gCursorOverlay;

static UINT32 *DrawBase(void) {
    if (gForceFront && gFront) {
        return gFront;
    }
    return gBackOn ? gBack : gFront;
}

static UINT32 DrawPitch(void) {
    if (gForceFront && gFront) {
        return gFrontPitch;
    }
    return gBackOn ? gBackPitch : gFrontPitch;
}

void VideoDrawBeginFront(void) {
    gForceFront = 1;
}

void VideoDrawEndFront(void) {
    gForceFront = 0;
}

void VideoReleaseBackbuffer(void) {
    if (gBack && gBackPages) {
        PhysicalMemoryFreePages(gBack, gBackPages);
    }
    gBack = 0;
    gBackPitch = 0;
    gBackPages = 0;
    gBackOn = 0;
    gDirty = 0;
    gCurDirty = 0;
}

UINT64 VideoFrameBufferBase(void) {
    return gScreen.FrameBufferBase;
}

UINT64 VideoFrameBufferSize(void) {
    return gScreen.FrameBufferSize;
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
     * 不从 GOP 全屏拷：后缓冲内容以后续绘制为准。
     * PR-K-log-cont：InitVideo 不再 ClearScreen+Present 抹掉 boot 上滚。
     */
    gDirty = 0;
    gCurDirty = 0;
}

int VideoBackbufferEnabled(void) {
    return gBackOn;
}

UINT32 VideoBackbufferPages(void) {
    return gBackPages;
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

/* 光标 XOR：再异或一次即擦除，无需 save-under/ReadPixel */
void VideoXorPixelRaw(UINT32 X, UINT32 Y, UINT32 Mask) {
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
    Fb[Y * Pitch + X] ^= Mask;
    /* 勿 DirtyUnion 进内容脏区：4K 下 Shell∪远处光标会并成近全屏 Present */
    DirtyUnionCursor(X, Y, 1, 1);
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

/* PR-GUI-alpha：Src 盖在 Dst 上（0x00RRGGBB）；无浮点 */
UINT32 VideoBlendRgb(UINT32 Dst, UINT32 Src, UINT8 Alpha) {
    UINT32 Inv;
    UINT32 R;
    UINT32 G;
    UINT32 B;

    if (Alpha == 0) {
        return Dst;
    }
    if (Alpha == 255) {
        return Src & 0x00FFFFFFu;
    }
    Inv = 255u - (UINT32)Alpha;
    R = (((Src >> 16) & 0xFFu) * (UINT32)Alpha + ((Dst >> 16) & 0xFFu) * Inv) / 255u;
    G = (((Src >> 8) & 0xFFu) * (UINT32)Alpha + ((Dst >> 8) & 0xFFu) * Inv) / 255u;
    B = ((Src & 0xFFu) * (UINT32)Alpha + (Dst & 0xFFu) * Inv) / 255u;
    return (R << 16) | (G << 8) | B;
}

void VideoBlendPixelRaw(UINT32 X, UINT32 Y, UINT32 Color, UINT8 Alpha) {
    UINT32 *Fb;
    UINT32 Pitch;
    UINT32 *Pix;

    if (Alpha == 0) {
        return;
    }
    if (Alpha == 255) {
        VideoDrawPixelRaw(X, Y, Color);
        return;
    }
    if (X >= gScreen.Width || Y >= gScreen.Height) {
        return;
    }
    Fb = DrawBase();
    Pitch = DrawPitch();
    if (!Fb || Pitch == 0) {
        return;
    }
    Pix = &Fb[Y * Pitch + X];
    *Pix = VideoBlendRgb(*Pix, Color, Alpha);
    DirtyUnion(X, Y, 1, 1);
}

void VideoBlendPixel(UINT32 X, UINT32 Y, UINT32 Color, UINT8 Alpha) {
    if (gClipOn) {
        if (X < gClipX || Y < gClipY ||
            X >= gClipX + gClipW || Y >= gClipY + gClipH) {
            return;
        }
    }
    VideoBlendPixelRaw(X, Y, Color, Alpha);
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

void VideoBlendFillRect(UINT32 X, UINT32 Y, UINT32 Width, UINT32 Height,
                        UINT32 Color, UINT8 Alpha) {
    UINT32 *Fb;
    UINT32 Pitch;
    UINT32 Row;
    UINT32 Col;
    UINT32 CopyW;
    UINT32 CopyH;
    UINT32 X0;
    UINT32 Y0;

    if (Alpha == 0 || !Width || !Height) {
        return;
    }
    if (Alpha == 255) {
        VideoFillRect(X, Y, Width, Height, Color);
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
            Line[Col] = VideoBlendRgb(Line[Col], Color, Alpha);
        }
    }
    DirtyUnion(X0, Y0, CopyW, CopyH);
}

void VideoCopyRect(UINT32 SrcX, UINT32 SrcY, UINT32 DstX, UINT32 DstY,
                   UINT32 Width, UINT32 Height) {
    UINT32 *Fb = DrawBase();
    UINT32 Pitch = DrawPitch();
    INT32 Y;
    INT32 W;
    INT32 H;
    UINT32 CopyW = Width;
    UINT32 CopyH = Height;
    UINTN RowBytes;

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

    RowBytes = (UINTN)W * sizeof(UINT32);
    /*
     * 按行 memcpy/memmove。勿对全屏做整块 memmove：4K 单次上滚约数十 MB，
     * 开机日志会极慢，且 live front 上仍可能被扫成「波浪」。
     */
    if (DstY == SrcY) {
        for (Y = 0; Y < H; Y++) {
            UINT32 *Dst = &Fb[(DstY + (UINT32)Y) * Pitch + DstX];
            UINT32 *Src = &Fb[(SrcY + (UINT32)Y) * Pitch + SrcX];
            memmove(Dst, Src, RowBytes);
        }
    } else if (DstY > SrcY) {
        for (Y = H - 1; Y >= 0; Y--) {
            UINT32 *Dst = &Fb[(DstY + (UINT32)Y) * Pitch + DstX];
            UINT32 *Src = &Fb[(SrcY + (UINT32)Y) * Pitch + SrcX];
            memcpy(Dst, Src, RowBytes);
        }
    } else {
        for (Y = 0; Y < H; Y++) {
            UINT32 *Dst = &Fb[(DstY + (UINT32)Y) * Pitch + DstX];
            UINT32 *Src = &Fb[(SrcY + (UINT32)Y) * Pitch + SrcX];
            memcpy(Dst, Src, RowBytes);
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

/* 内容下移一行（顶部空出），与 ScrollClip 相反 */
static void ScrollClipDown(void) {
    UINT32 *Fb = DrawBase();
    UINT32 Pitch = DrawPitch();
    UINT32 LineHeight = FontAdvanceY();
    UINT32 Y;
    UINT32 X;
    UINT32 XEnd;
    UINT32 YEnd;

    if (!Fb || Pitch == 0 || !gClipOn || gClipW == 0 || gClipH <= LineHeight) {
        return;
    }
    XEnd = gClipX + gClipW;
    YEnd = gClipY + gClipH;
    for (Y = YEnd; Y > gClipY + LineHeight; ) {
        Y--;
        for (X = gClipX; X < XEnd; X++) {
            Fb[Y * Pitch + X] = Fb[(Y - LineHeight) * Pitch + X];
        }
    }
    for (Y = gClipY; Y < gClipY + LineHeight; Y++) {
        for (X = gClipX; X < XEnd; X++) {
            Fb[Y * Pitch + X] = gClipBg;
        }
    }
    DirtyUnion(gClipX, gClipY, gClipW, gClipH);
}

void VideoScrollClipLines(int Delta) {
    int n;
    int i;

    if (!gClipOn || Delta == 0) {
        return;
    }
    n = Delta > 0 ? Delta : -Delta;
    if (n > 32) {
        n = 32;
    }
    for (i = 0; i < n; i++) {
        if (Delta > 0) {
            ScrollClipDown();
        } else {
            ScrollClip();
        }
    }
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
