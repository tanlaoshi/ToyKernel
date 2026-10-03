/*
 * FontTtfRaster.c — PR-UI-ttf-1：18px stb 栅格（须 HalFpuBegin；CFLAGS_FPU）
 */
#include "Font.h"
#include "Hal.h"
#include "ToySerialLog.h"

#if defined(__x86_64__)

#include <stddef.h>

#define TTF_CELL 18
#define TTF_PIX  (TTF_CELL * TTF_CELL)
#define TTF_ARENA (64u * 1024u)

static unsigned char gArena[TTF_ARENA];
static UINT32 gArenaUsed;
static int gInit;

static float FontTtfSqrtf(float X) {
    float R;

    __asm__ volatile("sqrtss %1, %0" : "=x"(R) : "x"(X));
    return R;
}

static void *FontTtfMalloc(size_t N) {
    UINT32 Need = (UINT32)((N + 15u) & ~15u);

    if (gArenaUsed + Need > TTF_ARENA) {
        return 0;
    }
    {
        void *P = gArena + gArenaUsed;
        gArenaUsed += Need;
        return P;
    }
}

static float FontTtfPowf(float X, float Y) {
    (void)X;
    (void)Y;
    return 0.0f;
}

static float FontTtfCosf(float X) {
    (void)X;
    return 1.0f;
}

#define STBTT_ifloor(x) ((int)(x))
#define STBTT_iceil(x)  ((int)((x) + 0.999999f))
#define STBTT_sqrt(x)   FontTtfSqrtf(x)
#define STBTT_fabs(x)   ((x) < 0 ? -(x) : (x))
#define STBTT_pow(x, y) FontTtfPowf((x), (y))
#define STBTT_cos(x)    FontTtfCosf(x)
#define STBTT_acos(x)   (0.0f)
#define STBTT_fmod(x, y) ((x) - (float)((int)((x) / (y))) * (y))
#define STBTT_malloc(x, u) FontTtfMalloc(x)
#define STBTT_free(x, u)   ((void)(x))
#define STBTT_assert(x) ((void)0)
#define STBTT_strlen(s) __builtin_strlen(s)
#define STBTT_memcpy(d, s, n) __builtin_memcpy((d), (s), (n))
#define STBTT_memset(d, v, n) __builtin_memset((d), (v), (n))
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

static stbtt_fontinfo gInfo;

int FontTtfInit(void) {
    const UINT8 *Blob;
    UINT32 Size;
    int Ok;

    gInit = 0;
    Blob = FontTtfBlob(&Size);
    if (!Blob || Size < 12u || !HalFpuOk()) {
        ToyLogBoot("Boot: ttf init skip\n");
        return -1;
    }
    if (!HalFpuBegin()) {
        ToyLogBoot("Boot: ttf init skip\n");
        return -1;
    }
    gArenaUsed = 0;
    Ok = stbtt_InitFont(&gInfo, Blob, 0);
    HalFpuEnd();
    if (!Ok) {
        ToyLogBoot("Boot: ttf init fail\n");
        return -1;
    }
    gInit = 1;
    ToyLogBoot("Boot: ttf init ok\n");
    return 0;
}

int FontTtfRasterCp(UINT32 Cp, UINT8 *Pix18) {
    float Scale;
    int X0;
    int Y0;
    int X1;
    int Y1;
    int Gw;
    int Gh;
    int Ox;
    int Oy;
    int Y;
    UINT8 Tmp[TTF_PIX];
    UINT32 i;

    if (!Pix18 || !gInit || Cp < 128u) {
        return -1;
    }
    for (i = 0; i < TTF_PIX; i++) {
        Pix18[i] = 0;
        Tmp[i] = 0;
    }
    gArenaUsed = 0;
    if (!HalFpuBegin()) {
        return -1;
    }
    /*
     * CJK 字面常吃不满 em 框，ScaleForPixelHeight(18) 目测比 Terminus 小约 30%。
     * 先放大再钳进 18 格，与点阵 gen 视觉量级对齐。
     */
    Scale = stbtt_ScaleForPixelHeight(&gInfo, (float)TTF_CELL) * 1.32f;
    stbtt_GetCodepointBitmapBox(&gInfo, (int)Cp, Scale, Scale, &X0, &Y0, &X1, &Y1);
    Gw = X1 - X0;
    Gh = Y1 - Y0;
    if (Gw <= 0 || Gh <= 0) {
        HalFpuEnd();
        return -1;
    }
    if (Gw > TTF_CELL || Gh > TTF_CELL) {
        float FitX = (float)TTF_CELL / (float)Gw;
        float FitY = (float)TTF_CELL / (float)Gh;
        float Fit = (FitX < FitY) ? FitX : FitY;

        Scale *= Fit * 0.98f;
        stbtt_GetCodepointBitmapBox(&gInfo, (int)Cp, Scale, Scale, &X0, &Y0, &X1,
                                    &Y1);
        Gw = X1 - X0;
        Gh = Y1 - Y0;
        if (Gw <= 0 || Gh <= 0 || Gw > TTF_CELL || Gh > TTF_CELL) {
            HalFpuEnd();
            return -1;
        }
    }
    Ox = (TTF_CELL - Gw) / 2;
    Oy = (TTF_CELL - Gh) / 2;
    if (Ox < 0) {
        Ox = 0;
    }
    if (Oy < 0) {
        Oy = 0;
    }
    stbtt_MakeCodepointBitmap(&gInfo, Tmp, Gw, Gh, TTF_CELL, Scale, Scale, (int)Cp);
    HalFpuEnd();
    for (Y = 0; Y < Gh; Y++) {
        int X;
        for (X = 0; X < Gw; X++) {
            Pix18[(Oy + Y) * TTF_CELL + (Ox + X)] = Tmp[Y * TTF_CELL + X];
        }
    }
    return 0;
}

#else

int FontTtfInit(void) {
    ToyLogBoot("Boot: ttf init skip\n");
    return -1;
}

int FontTtfRasterCp(UINT32 Cp, UINT8 *Pix18) {
    (void)Cp;
    (void)Pix18;
    return -1;
}

#endif
