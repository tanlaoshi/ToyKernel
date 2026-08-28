#include "Video.h"
#include "FontData.h"

static UINT32 *gFramebuffer = NULL;
static UINT32 gWidth = 0;
static UINT32 gHeight = 0;
static UINT32 gPixelsPerScanLine = 0;

void SetVideo(VIDEO_CONFIG *VideoConfig) {
    gFramebuffer = (UINT32 *)(UINTN)VideoConfig->FrameBufferBase;
    gWidth = VideoConfig->HorizontalResolution;
    gHeight = VideoConfig->VerticalResolution;
    gPixelsPerScanLine = VideoConfig->PixelsPerScanLine;
}

void DrawPixel(UINT32 X, UINT32 Y, UINT32 Color) {
    if (X >= gWidth || Y >= gHeight) return;
    if (gFramebuffer) {
        gFramebuffer[Y * gPixelsPerScanLine + X] = Color;
    }
}

void ClearScreen(UINT32 Color) {
    if (!gFramebuffer) return;
    for (UINT32 Y = 0; Y < gHeight; Y++) {
        for (UINT32 X = 0; X < gWidth; X++) {
            gFramebuffer[Y * gPixelsPerScanLine + X] = Color;
        }
    }
}

void DrawChar(UINT32 X, UINT32 Y, char c, UINT32 Color) {
    if (c < 32 || c > 126) return;
    const unsigned char *glyph = FontData[c - 32];
    for (int row = 0; row < 16; row++) {
        unsigned char row_bits = glyph[row];
        for (int col = 0; col < 8; col++) {
            if (row_bits & (1 << (7 - col))) {
                DrawPixel(X + col, Y + row, Color);
            }
        }
    }
}

void DrawString(UINT32 X, UINT32 Y, const char *str, UINT32 Color) {
    UINT32 cursor_x = X;
    UINT32 cursor_y = Y;
    UINT32 CharWidth = 8;
    UINT32 CharSpacing = 4;
    UINT32 LineHeight = 18;

    while (*str) {
        if (*str == '\n') {
            cursor_x = X;
            cursor_y += LineHeight;
        } else if (*str == '\r') {
            cursor_x = X;
        } else {
            DrawChar(cursor_x, cursor_y, *str, Color);
            cursor_x += CharWidth + CharSpacing;
        }
        str++;
    }
}