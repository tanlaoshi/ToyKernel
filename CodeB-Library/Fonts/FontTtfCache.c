/*
 * FontTtfCache.c — PR-UI-ttf-1/2：18×18×8bpp 定长缓存
 */
#include "Font.h"

#define TTF_CACHE_N 256u
#define TTF_CELL    18u
#define TTF_PIX     (TTF_CELL * TTF_CELL)

typedef struct {
    UINT32 Cp;
    int Used;
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
    if (S->Used && S->Cp == Cp) {
        if (OutW) {
            *OutW = TTF_CELL;
        }
        if (OutH) {
            *OutH = TTF_CELL;
        }
        return S->Pix;
    }
    if (FontTtfRasterCp(Cp, S->Pix) != 0) {
        S->Used = 0;
        return 0;
    }
    S->Cp = Cp;
    S->Used = 1;
    if (OutW) {
        *OutW = TTF_CELL;
    }
    if (OutH) {
        *OutH = TTF_CELL;
    }
    return S->Pix;
}
