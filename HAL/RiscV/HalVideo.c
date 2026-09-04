/*
 * HAL/RiscV/HalVideo.c — 帧缓冲门面桩（无 GOP；PR-A7 可链接）
 */
#include "HalVideo.h"

void HalVideoSet(const VIDEO_CONFIG *Config) { (void)Config; }
void HalVideoInitBackbuffer(void) { }
void HalVideoPresent(void) { }
int HalVideoBackbufferEnabled(void) { return 0; }

void HalVideoGetSize(UINT32 *Width, UINT32 *Height) {
    if (Width) {
        *Width = 0;
    }
    if (Height) {
        *Height = 0;
    }
}

void HalVideoDrawPixel(UINT32 X, UINT32 Y, UINT32 Color) {
    (void)X;
    (void)Y;
    (void)Color;
}

void HalVideoDrawPixelRaw(UINT32 X, UINT32 Y, UINT32 Color) {
    (void)X;
    (void)Y;
    (void)Color;
}

UINT32 HalVideoReadPixel(UINT32 X, UINT32 Y) {
    (void)X;
    (void)Y;
    return 0;
}

void HalVideoFillRect(UINT32 X, UINT32 Y, UINT32 Width, UINT32 Height, UINT32 Color) {
    (void)X;
    (void)Y;
    (void)Width;
    (void)Height;
    (void)Color;
}

void HalVideoCopyRect(UINT32 SrcX, UINT32 SrcY, UINT32 DstX, UINT32 DstY,
                      UINT32 Width, UINT32 Height) {
    (void)SrcX;
    (void)SrcY;
    (void)DstX;
    (void)DstY;
    (void)Width;
    (void)Height;
}

void HalVideoReadRect(UINT32 X, UINT32 Y, UINT32 Width, UINT32 Height, UINT32 *Out) {
    (void)X;
    (void)Y;
    (void)Width;
    (void)Height;
    (void)Out;
}

void HalVideoWriteRect(UINT32 X, UINT32 Y, UINT32 Width, UINT32 Height, const UINT32 *In) {
    (void)X;
    (void)Y;
    (void)Width;
    (void)Height;
    (void)In;
}

void HalVideoClearScreen(UINT32 Color) { (void)Color; }
void HalVideoDrawCharAt(UINT32 X, UINT32 Y, char C, UINT32 Color) {
    (void)X;
    (void)Y;
    (void)C;
    (void)Color;
}
void HalVideoDrawCodepointAt(UINT32 X, UINT32 Y, UINT32 Cp, UINT32 Color) {
    (void)X;
    (void)Y;
    (void)Cp;
    (void)Color;
}
void HalVideoDrawStringAt(UINT32 X, UINT32 Y, const char *Text, UINT32 Color) {
    (void)X;
    (void)Y;
    (void)Text;
    (void)Color;
}
void HalVideoDrawChar(char C, UINT32 Color) {
    (void)C;
    (void)Color;
}
void HalVideoDrawString(const char *Text, UINT32 Color) {
    (void)Text;
    (void)Color;
}
void HalVideoEraseLastChar(void) { }
void HalVideoSetClipRegion(UINT32 X, UINT32 Y, UINT32 Width, UINT32 Height, UINT32 Background) {
    (void)X;
    (void)Y;
    (void)Width;
    (void)Height;
    (void)Background;
}
void HalVideoSetClipOrigin(UINT32 X, UINT32 Y, UINT32 Width, UINT32 Height, UINT32 Background) {
    (void)X;
    (void)Y;
    (void)Width;
    (void)Height;
    (void)Background;
}
void HalVideoGetTextCursor(UINT32 *X, UINT32 *Y) {
    if (X) {
        *X = 0;
    }
    if (Y) {
        *Y = 0;
    }
}
void HalVideoSetTextCursor(UINT32 X, UINT32 Y) {
    (void)X;
    (void)Y;
}
void HalVideoClearClip(void) { }
