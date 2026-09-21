/*
 * ThemeSaveFmt.c — THEME.CFG / DB 数值格式化
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
