/*
 * FontTtfCache.c — PR-UI-ttf-1/2/3：18×18×8bpp 定长缓存
 *
 * 绘制路径只 Lookup（不栅格）。栅格仅 Preheat / Want 泵（Worker / 开窗短 burst）。
 */
#include "Font.h"

#define TTF_CACHE_N 1024u
#define TTF_PROBE   8u
#define TTF_CELL    18u
#define TTF_PIX     (TTF_CELL * TTF_CELL)
#define TTF_EMPTY   0
#define TTF_HIT     1
#define TTF_MISS    2
#define TTF_WANT_N  128u

typedef struct {
    UINT32 Cp;
    int State;
    UINT8 Pix[TTF_PIX];
} TTF_SLOT;

static TTF_SLOT gSlot[TTF_CACHE_N];
static UINT32 gWant[TTF_WANT_N];
static UINT32 gWantN;

static TTF_SLOT *SlotLookup(UINT32 Cp) {
    UINT32 i0 = Cp % TTF_CACHE_N;
    UINT32 k;

    for (k = 0; k < TTF_PROBE; k++) {
        TTF_SLOT *S = &gSlot[(i0 + k) % TTF_CACHE_N];

        if (S->State != TTF_EMPTY && S->Cp == Cp) {
            return S;
        }
    }
    return 0;
}

static TTF_SLOT *SlotAlloc(UINT32 Cp) {
    UINT32 i0 = Cp % TTF_CACHE_N;
    UINT32 k;
    TTF_SLOT *Empty = 0;

    for (k = 0; k < TTF_PROBE; k++) {
        TTF_SLOT *S = &gSlot[(i0 + k) % TTF_CACHE_N];

        if (S->State != TTF_EMPTY && S->Cp == Cp) {
            return S;
        }
        if (S->State == TTF_EMPTY && !Empty) {
            Empty = S;
        }
    }
    if (Empty) {
        return Empty;
    }
    /* 探测窗满：覆盖主槽 */
    return &gSlot[i0];
}

static void WantPush(UINT32 Cp) {
    UINT32 w;

    if (gWantN >= TTF_WANT_N) {
        return;
    }
    for (w = 0; w < gWantN; w++) {
        if (gWant[w] == Cp) {
            return;
        }
    }
    gWant[gWantN++] = Cp;
}

const UINT8 *FontTtfCacheGet(UINT32 Cp, UINT32 *OutW, UINT32 *OutH) {
    TTF_SLOT *S;

    if (Cp < 128u) {
        return 0;
    }
    S = SlotLookup(Cp);
    if (S) {
        if (S->State == TTF_MISS) {
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
    WantPush(Cp);
    return 0;
}

static void FillSlot(UINT32 Cp) {
    TTF_SLOT *Slot;

    if (Cp < 128u) {
        return;
    }
    Slot = SlotAlloc(Cp);
    if (Slot->State != TTF_EMPTY && Slot->Cp == Cp) {
        return;
    }
    if (FontTtfRasterCp(Cp, Slot->Pix) != 0) {
        Slot->Cp = Cp;
        Slot->State = TTF_MISS;
    } else {
        Slot->Cp = Cp;
        Slot->State = TTF_HIT;
    }
}

void FontTtfPreheatUtf8(const char *S) {
    while (S && *S) {
        UINT32 Cp;
        UINTN N;

        if (*S == '\n') {
            break;
        }
        N = Utf8Decode(S, &Cp);
        if (!N) {
            break;
        }
        if (Cp >= 128u) {
            FillSlot(Cp);
        }
        S += N;
    }
}

int FontTtfWantStep(void) {
    UINT32 Cp;

    if (gWantN == 0) {
        return 0;
    }
    Cp = gWant[0];
    gWantN--;
    {
        UINT32 w;
        for (w = 0; w < gWantN; w++) {
            gWant[w] = gWant[w + 1];
        }
    }
    FillSlot(Cp);
    return gWantN > 0 ? 1 : 0;
}

UINT32 FontTtfWantDrain(UINT32 Max) {
    UINT32 N = 0;

    if (Max == 0) {
        Max = 32u;
    }
    while (N < Max && gWantN > 0) {
        (void)FontTtfWantStep();
        N++;
    }
    return N;
}
