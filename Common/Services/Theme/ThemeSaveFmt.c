/*
 * ThemeSaveFmt.c — THEME.CFG / DB 数值格式化与 FillVals
 * 核心：ThemeSave.c
 */
#include "ThemePrivate.h"

void PutHex6(char *Dst, UINT32 Color) {
    static const char Hex[] = "0123456789abcdef";
    UINT32 C = Color & 0x00FFFFFFu;
    int i;

    for (i = 5; i >= 0; i--) {
        Dst[i] = Hex[C & 0xF];
        C >>= 4;
    }
}

void PutDec(char *Dst, UINT32 V, UINTN *Len) {
    char Tmp[8];
    int N = 0;
    int i;

    if (V == 0) {
        Dst[(*Len)++] = '0';
        return;
    }
    while (V > 0 && N < (int)sizeof(Tmp)) {
        Tmp[N++] = (char)('0' + (V % 10));
        V /= 10;
    }
    for (i = N - 1; i >= 0; i--) {
        Dst[(*Len)++] = Tmp[i];
    }
}

void ThemeSaveFillVals(THEME_SAVE_VALS *V) {
    UINTN N = 0;
    UINTN ModeLen = 0;
    UINTN ScaleLen = 0;
    UINTN FadeLen = 0;

    if (!V) {
        return;
    }
    V->FontVal[0] = 0;
    if (gFontId >= 10) {
        V->FontVal[N++] = (char)('0' + (gFontId / 10) % 10);
    }
    V->FontVal[N++] = (char)('0' + (gFontId % 10));
    V->FontVal[N] = 0;

    V->ModeVal[0] = 0;
    if (ThemeHasDisplayPref()) {
        PutDec(V->ModeVal, gModeW, &ModeLen);
        V->ModeVal[ModeLen++] = 'x';
        PutDec(V->ModeVal, gModeH, &ModeLen);
        V->ModeVal[ModeLen] = 0;
    }

    ScaleLen = 0;
    PutDec(V->ScaleVal, ThemeUiScale(), &ScaleLen);
    V->ScaleVal[ScaleLen] = 0;
    FadeLen = 0;
    PutDec(V->FadeVal, ThemeWindowFadeSteps(), &FadeLen);
    V->FadeVal[FadeLen] = 0;
    V->WallVal[0] = gWallpaper ? '1' : '0';
    V->WallVal[1] = 0;
    V->GradVal[0] = gDesktopGrad ? '1' : '0';
    V->GradVal[1] = 0;
}
