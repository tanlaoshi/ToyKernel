/*
 * FontTtfCache.c — PR-UI-ttf-1/2/3：18×18×8bpp 定长缓存
 *
 * 绘制路径只 Lookup（不栅格）。栅格仅 FontTtfPreheatUtf8（Worker）。
 */
#include "Font.h"

#define TTF_CACHE_N 256u
#define TTF_CELL    18u
#define TTF_PIX     (TTF_CELL * TTF_CELL)
#define TTF_EMPTY   0
#define TTF_HIT     1
#define TTF_MISS    2

typedef struct {
    UINT32 Cp;
    int State;
    UINT8 Pix[TTF_PIX];
} TTF_SLOT;

static TTF_SLOT gSlot[TTF_CACHE_N];

const UINT8 *FontTtfCacheGet(UINT32 Cp, UINT32 *OutW, UINT32 *OutH) {
    TTF_SLOT *S;
    UINT32 i;

    if (Cp < 128u) {
        return 0;
    }
    i = Cp % TTF_CACHE_N;
    S = &gSlot[i];
    if (S->State == TTF_EMPTY || S->Cp != Cp || S->State == TTF_MISS) {
        return 0;
    }
    if (OutW) {
        *OutW = TTF_CELL;
    }
    if (OutH) {
        *OutH = TTF_CELL;
    }
    return S->Pix;
}

void FontTtfPreheatUtf8(const char *S) {
    while (S && *S) {
        UINT32 Cp;
        UINTN N;
        TTF_SLOT *Slot;
        UINT32 i;

        if (*S == '\n') {
            break;
        }
        N = Utf8Decode(S, &Cp);
        if (!N) {
            break;
        }
        if (Cp >= 128u) {
            i = Cp % TTF_CACHE_N;
            Slot = &gSlot[i];
            if (Slot->State != TTF_EMPTY && Slot->Cp == Cp) {
                S += N;
                continue;
            }
            if (FontTtfRasterCp(Cp, Slot->Pix) != 0) {
                Slot->Cp = Cp;
                Slot->State = TTF_MISS;
            } else {
                Slot->Cp = Cp;
                Slot->State = TTF_HIT;
            }
        }
        S += N;
    }
}
