/*
 * Video.c — GOP 帧缓冲驱动
 *
 * 提供像素级绘制与终端式字符输出（自动换行、滚屏、退格擦除）。
 */
#include "Video.h"
#include "FontData.h"

static SCREEN_INFO gScreen = {0};
static UINT32 gBackground = 0x00000000;
static int gClipOn;
static UINT32 gClipX;
static UINT32 gClipY;
static UINT32 gClipW;
static UINT32 gClipH;
static UINT32 gClipBg;

/* 从 BOOT_CONFIG 设置全局屏幕参数 */
void VideoSet(VIDEO_CONFIG *VideoConfig) {
    gScreen.Width = VideoConfig->HorizontalResolution;
    gScreen.Height = VideoConfig->VerticalResolution;
    gScreen.PixelsPerScanLine = VideoConfig->PixelsPerScanLine;
    gScreen.FrameBufferBase = VideoConfig->FrameBufferBase;
    gScreen.FrameBufferSize = VideoConfig->FrameBufferSize;
    gScreen.CursorX = 0;
    gScreen.CursorY = 0;
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

/* 在 (X,Y) 绘制一个 ASCII 字符（按 FONT_PIXEL_SCALE 放大） */
void VideoDrawCharAt(UINT32 X, UINT32 Y, char C, UINT32 Color) {
    int Row;
    int Col;
    int Sy;
    int Sx;

    if (C < 32 || C > 126) {
        return;
    }
    const UINT8 *Glyph = FontData[C - 32];
    for (Row = 0; Row < FONT_HEIGHT; Row++) {
        for (Col = 0; Col < FONT_WIDTH; Col++) {
            UINT8 Byte = Glyph[Row * FONT_BYTES_PER_ROW + (Col / 8)];
            int Bit = 7 - (Col % 8);
            if ((Byte & (1 << Bit)) == 0) {
                continue;
            }
            for (Sy = 0; Sy < FONT_PIXEL_SCALE; Sy++) {
                for (Sx = 0; Sx < FONT_PIXEL_SCALE; Sx++) {
                    VideoDrawPixel(
                        X + (UINT32)(Col * FONT_PIXEL_SCALE + Sx),
                        Y + (UINT32)(Row * FONT_PIXEL_SCALE + Sy),
                        Color);
                }
            }
        }
    }
}

void VideoDrawStringAt(UINT32 X, UINT32 Y, const char *Text, UINT32 Color) {
    UINT32 CurX = X;
    while (*Text) {
        if (*Text == '\n') {
            CurX = X;
            Y += FONT_ADVANCE_Y;
        } else {
            VideoDrawCharAt(CurX, Y, *Text, Color);
            CurX += FONT_ADVANCE_X;
        }
        Text++;
    }
}

/* 在 (X,Y) 绘制一个像素（ARGB 格式，越界忽略） */
void VideoDrawPixel(UINT32 X, UINT32 Y, UINT32 Color) {
    if (X >= gScreen.Width || Y >= gScreen.Height) return;
    UINT32 *Framebuffer = (UINT32*)(UINTN)gScreen.FrameBufferBase;
    if (Framebuffer) {
        Framebuffer[Y * gScreen.PixelsPerScanLine + X] = Color;
    }
}

UINT32 VideoReadPixel(UINT32 X, UINT32 Y) {
    if (X >= gScreen.Width || Y >= gScreen.Height) {
        return 0;
    }
    UINT32 *Framebuffer = (UINT32*)(UINTN)gScreen.FrameBufferBase;
    if (!Framebuffer) {
        return 0;
    }
    return Framebuffer[Y * gScreen.PixelsPerScanLine + X];
}

void VideoFillRect(UINT32 X, UINT32 Y, UINT32 Width, UINT32 Height, UINT32 Color) {
    UINT32 *Framebuffer;
    UINT32 Row;
    UINT32 Col;

    if (!Width || !Height) {
        return;
    }
    Framebuffer = (UINT32 *)(UINTN)gScreen.FrameBufferBase;
    if (!Framebuffer) {
        return;
    }
    for (Row = 0; Row < Height; Row++) {
        UINT32 Py = Y + Row;
        if (Py >= gScreen.Height) {
            break;
        }
        for (Col = 0; Col < Width; Col++) {
            UINT32 Px = X + Col;
            if (Px >= gScreen.Width) {
                break;
            }
            Framebuffer[Py * gScreen.PixelsPerScanLine + Px] = Color;
        }
    }
}

/*
 * 拷贝矩形像素（可重叠）。用于窗口拖动时整体平移，保留客户区文字。
 */
void VideoCopyRect(UINT32 SrcX, UINT32 SrcY, UINT32 DstX, UINT32 DstY,
                   UINT32 Width, UINT32 Height) {
    UINT32 *Fb = (UINT32 *)(UINTN)gScreen.FrameBufferBase;
    UINT32 Pitch;
    INT32 Y;
    INT32 X;
    INT32 W;
    INT32 H;
    UINT32 CopyW = Width;
    UINT32 CopyH = Height;

    if (!Fb || !Width || !Height) {
        return;
    }
    if (SrcX >= gScreen.Width || SrcY >= gScreen.Height ||
        DstX >= gScreen.Width || DstY >= gScreen.Height) {
        return;
    }
    Pitch = gScreen.PixelsPerScanLine;

    /* 源与目标各自可拷贝的宽高取交集，避免一边裁切导致错位 */
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

    if (DstY > SrcY || (DstY == SrcY && DstX > SrcX)) {
        for (Y = H - 1; Y >= 0; Y--) {
            for (X = W - 1; X >= 0; X--) {
                Fb[(DstY + (UINT32)Y) * Pitch + DstX + (UINT32)X] =
                    Fb[(SrcY + (UINT32)Y) * Pitch + SrcX + (UINT32)X];
            }
        }
    } else {
        for (Y = 0; Y < H; Y++) {
            for (X = 0; X < W; X++) {
                Fb[(DstY + (UINT32)Y) * Pitch + DstX + (UINT32)X] =
                    Fb[(SrcY + (UINT32)Y) * Pitch + SrcX + (UINT32)X];
            }
        }
    }
}

void VideoReadRect(UINT32 X, UINT32 Y, UINT32 Width, UINT32 Height, UINT32 *Out) {
    UINT32 *Fb = (UINT32 *)(UINTN)gScreen.FrameBufferBase;
    UINT32 Pitch;
    UINT32 Row;
    UINT32 Col;
    UINT32 i = 0;

    if (!Fb || !Out || !Width || !Height) {
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
    Pitch = gScreen.PixelsPerScanLine;
    for (Row = 0; Row < Height; Row++) {
        for (Col = 0; Col < Width; Col++) {
            Out[i++] = Fb[(Y + Row) * Pitch + X + Col];
        }
    }
}

void VideoWriteRect(UINT32 X, UINT32 Y, UINT32 Width, UINT32 Height, const UINT32 *In) {
    UINT32 *Fb = (UINT32 *)(UINTN)gScreen.FrameBufferBase;
    UINT32 Pitch;
    UINT32 Row;
    UINT32 SrcStride;
    UINT32 CopyW;
    UINT32 CopyH;

    if (!Fb || !In || !Width || !Height) {
        return;
    }
    if (X >= gScreen.Width || Y >= gScreen.Height) {
        return;
    }
    /* 源缓冲按调用方 Width 紧密排列；裁剪后仍须用原 stride 换行 */
    SrcStride = Width;
    CopyW = Width;
    CopyH = Height;
    if (X + CopyW > gScreen.Width) {
        CopyW = gScreen.Width - X;
    }
    if (Y + CopyH > gScreen.Height) {
        CopyH = gScreen.Height - Y;
    }
    Pitch = gScreen.PixelsPerScanLine;
    for (Row = 0; Row < CopyH; Row++) {
        UINT32 *Dst = &Fb[(Y + Row) * Pitch + X];
        const UINT32 *Src = &In[Row * SrcStride];
        UINT32 Col;

        for (Col = 0; Col < CopyW; Col++) {
            Dst[Col] = Src[Col];
        }
    }
}

/* 用指定颜色填充整个屏幕并重置光标 */
void VideoClearScreen(UINT32 Color) {
    if (!gScreen.FrameBufferBase) {
        return;
    }
    VideoFillRect(0, 0, gScreen.Width, gScreen.Height, Color);
    gScreen.CursorX = 0;
    gScreen.CursorY = 0;
    gBackground = Color;
}

/* 文本区域向上滚动一行 */
static void ScrollClip(void) {
    UINT32 *Framebuffer = (UINT32 *)(UINTN)gScreen.FrameBufferBase;
    UINT32 LineHeight = FONT_ADVANCE_Y;
    UINT32 Y;
    UINT32 X;
    UINT32 XEnd;
    UINT32 YEnd;

    if (!Framebuffer || gClipW == 0 || gClipH <= LineHeight) {
        return;
    }
    XEnd = gClipX + gClipW;
    YEnd = gClipY + gClipH;
    for (Y = gClipY; Y + LineHeight < YEnd; Y++) {
        for (X = gClipX; X < XEnd; X++) {
            Framebuffer[Y * gScreen.PixelsPerScanLine + X] =
                Framebuffer[(Y + LineHeight) * gScreen.PixelsPerScanLine + X];
        }
    }
    for (Y = YEnd - LineHeight; Y < YEnd; Y++) {
        for (X = gClipX; X < XEnd; X++) {
            Framebuffer[Y * gScreen.PixelsPerScanLine + X] = gClipBg;
        }
    }
}

static void ScrollScreen(void) {
    UINT32 *Framebuffer = (UINT32*)(UINTN)gScreen.FrameBufferBase;
    if (!Framebuffer) {
        return;
    }
    if (gClipOn) {
        ScrollClip();
        return;
    }

    UINT32 LineHeight = FONT_ADVANCE_Y;
    
    for (UINT32 Y = 0; Y < gScreen.Height - LineHeight; Y++) {
        for (UINT32 X = 0; X < gScreen.PixelsPerScanLine; X++) {
            Framebuffer[Y * gScreen.PixelsPerScanLine + X] = 
                Framebuffer[(Y + LineHeight) * gScreen.PixelsPerScanLine + X];
        }
    }
    
    for (UINT32 Y = gScreen.Height - LineHeight; Y < gScreen.Height; Y++) {
        for (UINT32 X = 0; X < gScreen.PixelsPerScanLine; X++) {
            Framebuffer[Y * gScreen.PixelsPerScanLine + X] = gBackground;
        }
    }
}

/* 换行：光标移到下一行，必要时滚屏 */
void VideoNewLine(void) {
    UINT32 LineHeight = FONT_ADVANCE_Y;

    if (!gScreen.FrameBufferBase || gScreen.Height == 0 || gScreen.Width == 0) {
        return;
    }

    if (gClipOn) {
        if (gClipH <= LineHeight) {
            /* 仍回到行首，避免 \n 被吞后提示符粘在同一行 */
            gScreen.CursorX = gClipX;
            return;
        }
        gScreen.CursorX = gClipX;
        gScreen.CursorY += LineHeight;
        while (gScreen.CursorY + FONT_CELL_H > gClipY + gClipH) {
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

    while (gScreen.CursorY + FONT_CELL_H > gScreen.Height) {
        ScrollScreen();
        if (gScreen.CursorY < LineHeight) {
            gScreen.CursorY = 0;
            break;
        }
        gScreen.CursorY -= LineHeight;
    }
}

/* 在当前光标处绘制一个 ASCII 字符 */
void VideoDrawChar(char c, UINT32 Color) {
    UINT32 MaxX;
    UINT32 MaxY;

    if (c < 32 || c > 126) {
        return;
    }
    if (!gScreen.FrameBufferBase || gScreen.Width == 0 || gScreen.Height == 0) {
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
    
    if (gScreen.CursorX + FONT_ADVANCE_X > MaxX) {
        VideoNewLine();
    }
    
    if (gScreen.CursorY + FONT_CELL_H > MaxY) {
        VideoNewLine();
    }
    
    VideoDrawCharAt(gScreen.CursorX, gScreen.CursorY, c, Color);
    gScreen.CursorX += FONT_ADVANCE_X;
}

/* 擦除光标前一个字符（用背景色覆盖） */
void VideoEraseLastChar(void) {
    UINT32 Step = FONT_ADVANCE_X;
    UINT32 MinX = gClipOn ? gClipX : 0;

    if (gScreen.CursorX < MinX + Step) {
        return;
    }
    gScreen.CursorX -= Step;
    for (UINT32 Row = 0; Row < FONT_CELL_H; Row++) {
        for (UINT32 Col = 0; Col < Step; Col++) {
            VideoDrawPixel(gScreen.CursorX + Col, gScreen.CursorY + Row, gBackground);
        }
    }
}

/* 绘制字符串，支持 \n 换行 */
void VideoDrawString(const char *Text, UINT32 Color) {
    while (*Text) {
        if (*Text == '\n') {
            VideoNewLine();
        } else {
            VideoDrawChar(*Text, Color);
        }
        Text++;
    }
}
