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
void SetVideo(VIDEO_CONFIG *VideoConfig) {
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

void VideoSetClipRegion(UINT32 X, UINT32 Y, UINT32 W, UINT32 H, UINT32 Bg) {
    gClipOn = 1;
    gClipX = X;
    gClipY = Y;
    gClipW = W;
    gClipH = H;
    gClipBg = Bg;
    gBackground = Bg;
}

void VideoSetClipOrigin(UINT32 X, UINT32 Y, UINT32 W, UINT32 H, UINT32 Bg) {
    VideoSetClipRegion(X, Y, W, H, Bg);
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

/* 在 (X,Y) 绘制一个 ASCII 字符 */
void DrawCharAt(UINT32 X, UINT32 Y, char C, UINT32 Color) {
    if (C < 32 || C > 126) {
        return;
    }
    const UINT8 *Glyph = FontData[C - 32];
    for (int Row = 0; Row < FONT_HEIGHT; Row++) {
        UINT8 RowBits = Glyph[Row];
        for (int Col = 0; Col < FONT_WIDTH; Col++) {
            if (RowBits & (1 << (FONT_WIDTH - 1 - Col))) {
                DrawPixel(X + (UINT32)Col, Y + (UINT32)Row, Color);
            }
        }
    }
}

void DrawStringAt(UINT32 X, UINT32 Y, const char *Text, UINT32 Color) {
    UINT32 CurX = X;
    while (*Text) {
        if (*Text == '\n') {
            CurX = X;
            Y += FONT_HEIGHT + FONT_LINE_SPACING;
        } else {
            DrawCharAt(CurX, Y, *Text, Color);
            CurX += FONT_WIDTH + FONT_CHAR_SPACING;
        }
        Text++;
    }
}

/* 在 (X,Y) 绘制一个像素（ARGB 格式，越界忽略） */
void DrawPixel(UINT32 X, UINT32 Y, UINT32 Color) {
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

/* 用指定颜色填充整个屏幕并重置光标 */
void ClearScreen(UINT32 Color) {
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
    UINT32 LineHeight = FONT_HEIGHT + FONT_LINE_SPACING;
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

    UINT32 LineHeight = FONT_HEIGHT + FONT_LINE_SPACING;
    
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
void NewLine(void) {
    UINT32 LineHeight = FONT_HEIGHT + FONT_LINE_SPACING;

    if (gClipOn) {
        gScreen.CursorX = gClipX;
        gScreen.CursorY += LineHeight;
        while (gScreen.CursorY + FONT_HEIGHT > gClipY + gClipH) {
            ScrollClip();
            gScreen.CursorY -= LineHeight;
        }
        return;
    }

    gScreen.CursorX = 0;
    gScreen.CursorY += LineHeight;
    
    while (gScreen.CursorY + FONT_HEIGHT > gScreen.Height) {
        ScrollScreen();
        gScreen.CursorY -= LineHeight;
    }
}

/* 在当前光标处绘制一个 ASCII 字符 */
void DrawChar(char c, UINT32 Color) {
    UINT32 MaxX;
    UINT32 MaxY;

    if (c < 32 || c > 126) {
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
    
    if (gScreen.CursorX + FONT_WIDTH + FONT_CHAR_SPACING > MaxX) {
        NewLine();
    }
    
    if (gScreen.CursorY + FONT_HEIGHT > MaxY) {
        NewLine();
    }
    
    const UINT8 *Glyph = FontData[c - 32];
    for (int Row = 0; Row < FONT_HEIGHT; Row++) {
        UINT8 RowBits = Glyph[Row];
        for (int Col = 0; Col < FONT_WIDTH; Col++) {
            if (RowBits & (1 << (FONT_WIDTH - 1 - Col))) {
                DrawPixel(gScreen.CursorX + Col, gScreen.CursorY + Row, Color);
            }
        }
    }
    
    gScreen.CursorX += FONT_WIDTH + FONT_CHAR_SPACING;
}

/* 擦除光标前一个字符（用背景色覆盖） */
void EraseLastChar(void) {
    UINT32 Step = FONT_WIDTH + FONT_CHAR_SPACING;
    UINT32 MinX = gClipOn ? gClipX : 0;

    if (gScreen.CursorX < MinX + Step) {
        return;
    }
    gScreen.CursorX -= Step;
    for (UINT32 Row = 0; Row < FONT_HEIGHT; Row++) {
        for (UINT32 Col = 0; Col < Step; Col++) {
            DrawPixel(gScreen.CursorX + Col, gScreen.CursorY + Row, gBackground);
        }
    }
}

/* 绘制字符串，支持 \n 换行 */
void DrawString(const char *Text, UINT32 Color) {
    while (*Text) {
        if (*Text == '\n') {
            NewLine();
        } else {
            DrawChar(*Text, Color);
        }
        Text++;
    }
}
